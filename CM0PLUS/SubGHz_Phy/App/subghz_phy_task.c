/*!
 * \file      subghz_phy_task.c
 *
 * \brief     SubGHz PHY task registration — initialises all CM0+ protocol
 *            machines and registers the TDMA slot task with UTIL_SEQ.
 *
 * \code
 *              ____  ______  _         ___   _   _  __  __
 *             / ___||  ____|| |       |_ _| | | | ||  \/  |
 *            | |    | |__   | |        | |  | | | || |\/| |
 *            | |___ |  __|  | |___    _| |_ | |_| || |  | |
 *             \____||______| \_____| |_____| \___/ |_|  |_|
 *            (C)2025-2026 Celium
 *
 * \endcode
 *
 * \author    Simon R.C. Langlais ( Celium )
 *
 */
#include "subghz_phy_task.h"
#include "utilities_def.h"
#include "stm32_seq.h"
#include "sys_app.h"          /* APP_LOG, TS_ON/TS_OFF, VLEVEL_M/VLEVEL_H */
#include "stm32_timer.h"      /* UTIL_TIMER_GetCurrentTime */
#include "radio.h"            /* Radio */
#include "radio_def.h"        /* MODEM_LORA */
#include "rtc.h"              /* hrtc */
#include "main.h"             /* RTC_PREDIV_S */
#include "tdma_machine.h"
#include "mac_state_machine.h"
#include "freq_resolver.h"
#include "compliance_engine.h"
#include "shared_mem.h"
#include "protocol_types.h"   /* NODE_CLASS_C* constants */
#include "tdma_table.h"       /* TdmaTable_PhaseCount */

/* =========================================================================
 * Radio event callbacks — bridge radio ISR events to MAC layer
 * ========================================================================= */

static RadioEvents_t s_radio_events;

static void on_tx_done(void)
{
    APP_LOG(TS_ON, VLEVEL_H, "Radio: TxDone\r\n");
}

static void on_tx_timeout(void)
{
    APP_LOG(TS_ON, VLEVEL_H, "Radio: TxTimeout\r\n");
}

static void on_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    APP_LOG(TS_ON, VLEVEL_H, "Radio: RxDone size=%u rssi=%d snr=%d\r\n",
            (unsigned)size, (int)rssi, (int)snr);

    if (size == (uint16_t)sizeof(SyncPayload_t)) {
        MAC_OnSyncPacketReceived((const SyncPayload_t *)payload,
                                  (uint32_t)UTIL_TIMER_GetCurrentTime());
    } else if (size == (uint16_t)sizeof(BeaconPayload_t)) {
        MAC_OnBeaconReceived((const BeaconPayload_t *)payload);
    }
}

static void on_rx_timeout(void)
{
    APP_LOG(TS_ON, VLEVEL_H, "Radio: RxTimeout\r\n");
}

static void on_rx_error(void)
{
    APP_LOG(TS_ON, VLEVEL_H, "Radio: RxError\r\n");
}

/* =========================================================================
 * Platform callbacks for TdmaMachine_Init
 * ========================================================================= */

/* GetTimerTicks (timer_if.c) returns:
 *   (h*3600 + m*60 + s)*1000 + (PREDIV_S - SSR)*1000/(PREDIV_S+1)
 * UTIL_TIMER_GetCurrentTime is a thin wrapper around GetTimerTicks (identity
 * Tick2ms conversion). Both return ms-since-midnight. */
static uint32_t plat_get_rtc_ms(void)
{
    return (uint32_t)UTIL_TIMER_GetCurrentTime();
}

/* ProgramAlarmA — converts an absolute ms-since-midnight value back to
 * RTC register fields and arms Alarm A.
 *
 * abs_ms is in the same epoch as plat_get_rtc_ms() (ms since midnight,
 * wraps at 86 400 000 ms).  The inverse of GetTimerTicks:
 *
 *   target_s   = (abs_ms / 1000) % 86400
 *   target_ssr = PREDIV_S - target_ms * (PREDIV_S+1) / 1000
 *
 * Alarm A fires when the RTC calendar matches hours/minutes/seconds/subseconds.
 * Date/weekday is masked out — only the time fields are compared.
 *
 * The UTIL_TIMER infrastructure drives the WakeUp Timer (WUT); Alarm A is
 * fully available for the TDMA Machine.
 */
static void plat_program_alarm_a(uint32_t abs_ms)
{
    uint32_t target_s   = (abs_ms / 1000u) % 86400u;
    uint32_t target_ms  = abs_ms % 1000u;
    uint32_t target_ssr = RTC_PREDIV_S
                        - (target_ms * (RTC_PREDIV_S + 1u)) / 1000u;

    RTC_AlarmTypeDef alarm;
    alarm.AlarmTime.Hours          = (uint8_t)(target_s / 3600u);
    alarm.AlarmTime.Minutes        = (uint8_t)((target_s % 3600u) / 60u);
    alarm.AlarmTime.Seconds        = (uint8_t)(target_s % 60u);
    alarm.AlarmTime.SubSeconds     = target_ssr;
    alarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    alarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
    alarm.AlarmMask                = RTC_ALARMMASK_DATEWEEKDAY;
    alarm.AlarmSubSecondMask       = RTC_ALARMSUBSECONDMASK_NONE;
    alarm.AlarmDateWeekDaySel      = RTC_ALARMDATEWEEKDAYSEL_DATE;
    alarm.AlarmDateWeekDay         = 1u;  /* masked out by RTC_ALARMMASK_DATEWEEKDAY */
    alarm.Alarm                    = RTC_ALARM_A;

    hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
    HAL_RTC_SetAlarm_IT(&hrtc, &alarm, RTC_FORMAT_BIN);
}

static void     plat_radio_set_channel(uint32_t hz)            { Radio.SetChannel(hz);              }
static void     plat_radio_send(const uint8_t *b, uint8_t l)   { Radio.Send((uint8_t *)b, l);       }
static void     plat_radio_set_rx(uint32_t tmo)                { Radio.Rx(tmo);                     }
static void     plat_radio_sleep(void)                         { Radio.Sleep();                     }

/* ToA for SF12/BW125 Sync packet (11-byte SyncPayload, 8-symbol preamble).
 * Replace parameters once the radio is fully configured. */
static uint32_t plat_radio_toa(void)
{
    return Radio.TimeOnAir(MODEM_LORA,
                           0u,     /* BW 125 kHz  */
                           12u,    /* SF12        */
                           1u,     /* CR 4/5      */
                           8u,     /* preamble symbols */
                           false,  /* variable length */
                           11u,    /* SyncPayload = 11 bytes */
                           true);  /* CRC on */
}

/* =========================================================================
 * MAC State Machine hooks
 * ========================================================================= */

/* Packet 1 / Tier 3 hook: decompose binary target_ms to H:M:S; apply HAL_RTC_SetTime(BIN).
 * Sub-second component (target_ms % 1000) is aligned via SHIFTR ADD1S after SetTime:
 * set one second early, then ADD1S=1 advances calendar to target_s with SSR = SUBFS. */
static void mac_hook_rtc_set(uint32_t target_ms,
                              uint8_t  day, uint8_t month, uint8_t year)
{
    uint32_t target_s  = (target_ms / 1000u) % 86400u;
    uint32_t subsec_ms = target_ms % 1000u;

    /* Set one second early when sub-second adjustment is needed.
     * Midnight edge (target_s == 0) skipped to avoid date roll-back. */
    uint32_t set_s = (subsec_ms > 0u && target_s > 0u) ? target_s - 1u : target_s;

    RTC_TimeTypeDef t = {0};
    t.Hours          = (uint8_t)(set_s / 3600u);
    t.Minutes        = (uint8_t)((set_s % 3600u) / 60u);
    t.Seconds        = (uint8_t)(set_s % 60u);
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;

    RTC_DateTypeDef d = {0};
    d.Date  = day;
    d.Month = month;
    d.Year  = year;

    hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
    HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BCD);
    /* SSR = PREDIV_S (start of set_s) after SetTime */

    APP_LOG(TS_OFF, VLEVEL_M,
            "MAC: rtc_set %02u:%02u:%02u.%03u\r\n",
            (unsigned)t.Hours, (unsigned)t.Minutes,
            (unsigned)t.Seconds, (unsigned)subsec_ms);

    /* Sub-second alignment via SHIFTR.
     * ADD1S=1 advances calendar by 1 s and sets SSR = SUBFS, so
     * elapsed-in-second = (PREDIV_S − SUBFS)/(PREDIV_S+1) ≈ subsec_ms/1000. */
    if (subsec_ms > 0u && target_s > 0u) {
        hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
        if (!READ_BIT(hrtc.Instance->ICSR, RTC_ICSR_SHPF)) {
            uint32_t shift_ticks = (subsec_ms * (RTC_PREDIV_S + 1u)) / 1000u;
            HAL_RTCEx_SetSynchroShift(&hrtc, RTC_SHIFTADD1S_SET,
                                       (RTC_PREDIV_S + 1u) - shift_ticks);
        }
    }
}

/* Tier 2 hook: apply SSR-only correction when CLOCK_WARM and 8ms ≤ error < 300ms.
 * Direction: error_ms > 0 → RTC fast → delay (ADD1S=0).
 *            error_ms < 0 → RTC slow → advance (ADD1S=1, SSR set to SUBFS). */
static void mac_hook_rtc_align_sub(uint32_t preamble_timestamp_ms,
                                    uint32_t expected_offset_ms)
{
    int32_t error_ms = (int32_t)preamble_timestamp_ms - (int32_t)expected_offset_ms;
    APP_LOG(TS_OFF, VLEVEL_M,
            "MAC: rtc_align_sub preamble=%u expected=%u err=%d ms\r\n",
            (unsigned)preamble_timestamp_ms, (unsigned)expected_offset_ms,
            (int)error_ms);

    if (error_ms == 0) return;

    hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
    if (READ_BIT(hrtc.Instance->ICSR, RTC_ICSR_SHPF)) return;  /* shift pending */
    if (hrtc.Instance->SSR & 0x8000u) return;                  /* SS[15] guard (AN4759) */

    uint32_t error_abs   = (error_ms < 0) ? (uint32_t)(-error_ms) : (uint32_t)(error_ms);
    uint32_t shift_ticks = (error_abs * (RTC_PREDIV_S + 1u)) / 1000u;

    if (error_ms > 0) {
        /* RTC fast → delay: SUBFS added to SSR prescaler counter */
        HAL_RTCEx_SetSynchroShift(&hrtc, RTC_SHIFTADD1S_RESET, shift_ticks);
    } else {
        /* RTC slow → advance: ADD1S=1 sets SSR = SUBFS after +1 calendar second.
         * Advance = 1 − SUBFS/(PREDIV_S+1) = shift_ticks/(PREDIV_S+1)  (AN4759 §2.6) */
        HAL_RTCEx_SetSynchroShift(&hrtc, RTC_SHIFTADD1S_SET,
                                   (RTC_PREDIV_S + 1u) - shift_ticks);
    }
}

/* Atomic RTC snapshot hook: called at Sync Phase entry (C3/C2 TX epoch
 * capture) and right after mac_hook_rtc_set (C2/C1 Packet 1 path) to read
 * back the new RTC domain. HAL_RTC_GetTime must precede HAL_RTC_GetDate -
 * it unlocks the calendar shadow registers (see sys_app.c SystemApp_Init). */
static void mac_hook_get_rtc_snapshot(uint32_t *ms,
                                       uint8_t  *day, uint8_t *month, uint8_t *year)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BCD);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BCD);

    *ms    = (uint32_t)UTIL_TIMER_GetCurrentTime();
    *day   = d.Date;
    *month = d.Month;
    *year  = d.Year;
}

/* Packet 1 hook: re-anchor the TDMA Machine's FrameCursor and alarm chain
 * to the cell the Sync packet was actually received in, in the RTC domain
 * mac_hook_rtc_set just switched to. */
static void mac_hook_sync_bootstrapped(uint8_t  sync_phase_idx,
                                        uint8_t  sync_cell_idx,
                                        uint32_t rtc_now_ms)
{
    APP_LOG(TS_OFF, VLEVEL_M,
            "MAC: sync_bootstrapped phase=%u cell=%u rtc_now=%u ms\r\n",
            (unsigned)sync_phase_idx, (unsigned)sync_cell_idx,
            (unsigned)rtc_now_ms);
    TdmaMachine_BootstrapFromSync(sync_phase_idx, sync_cell_idx, rtc_now_ms);
}

static void mac_hook_sync_locked(void)
{
    APP_LOG(TS_OFF, VLEVEL_M, "MAC: CLOCK_WARM — sync locked\r\n");
}

static void mac_hook_sync_lost(void)
{
    APP_LOG(TS_OFF, VLEVEL_M, "MAC: CLOCK_COLD — sync lost\r\n");
}

static const MAC_Hooks_t s_mac_hooks = {
    .rtc_set             = mac_hook_rtc_set,
    .rtc_align_subsecond = mac_hook_rtc_align_sub,
    .get_rtc_snapshot    = mac_hook_get_rtc_snapshot,
    .sync_bootstrapped   = mac_hook_sync_bootstrapped,
    .sync_locked         = mac_hook_sync_locked,
    .sync_lost           = mac_hook_sync_lost,
};

/* =========================================================================
 * HAL Alarm A callback — bridges RTC ISR to sequencer
 *
 * HAL_RTC_AlarmAEventCallback is a weak symbol in the HAL; this definition
 * overrides it.  Called from RTC_LSECSS_IRQHandler → HAL_RTC_AlarmIRQHandler.
 * ========================================================================= */

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc_arg)
{
    (void)hrtc_arg;
    UTIL_SEQ_SetTask(1u << CFG_SEQ_Task_TdmaSlotWake, CFG_SEQ_Prio_0);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void SubGhzPhyTask_Init(void)
{
    /* 0. Zero the inter-core SRAM2 shared region before any machine touches it.
     *    CM4 writes its fields (g_freq_resolver_state, g_alarm_b_request) after
     *    this point, before the first TDMA slot fires. */
    SharedMem_Init();

    APP_LOG(TS_OFF, VLEVEL_M,
            "SubGhzPhyTask: init NODE_CLASS=%u\r\n", (unsigned)NODE_CLASS);

    /* 1. Radio — must be initialised before any UTIL_TIMER usage.
     *    TxTimeoutTimer and RxTimeoutTimer inside the radio driver are created
     *    here; without this call their Callback fields stay NULL (BSS zero),
     *    and the first Radio.Rx() would enqueue a NULL-callback timer →
     *    HardFault at UTIL_TIMER_IRQ_Handler. */
    s_radio_events.TxDone           = on_tx_done;
    s_radio_events.TxTimeout        = on_tx_timeout;
    s_radio_events.RxDone           = on_rx_done;
    s_radio_events.RxTimeout        = on_rx_timeout;
    s_radio_events.RxError          = on_rx_error;
    s_radio_events.FhssChangeChannel = NULL;
    s_radio_events.CadDone          = NULL;
    Radio.Init(&s_radio_events);

    /* Configure modem: LoRa SF12 / BW125 / CR4-5 / 14 dBm / CRC on.
     * bandwidth index 0 = 125 kHz, datarate = SF, coderate 1 = CR 4/5. */
    Radio.SetTxConfig(MODEM_LORA,
                      14,    /* power dBm  */
                      0u,    /* fdev (FSK only) */
                      0u,    /* bandwidth index: 0 = 125 kHz */
                      12u,   /* datarate: SF12 */
                      1u,    /* coderate: CR 4/5 */
                      8u,    /* preamble symbols */
                      false, /* fixed length */
                      true,  /* CRC on */
                      false, /* FHSS off */
                      0u,    /* hop period */
                      false, /* IQ not inverted */
                      3000u);/* TX timeout ms */

    Radio.SetRxConfig(MODEM_LORA,
                      0u,    /* bandwidth index: 0 = 125 kHz */
                      12u,   /* datarate: SF12 */
                      1u,    /* coderate: CR 4/5 */
                      0u,    /* bandwidthAfc (FSK only) */
                      8u,    /* preamble symbols */
                      5u,    /* symbol timeout */
                      false, /* fixed length */
                      0u,    /* payload length (variable) */
                      true,  /* CRC on */
                      false, /* FHSS off */
                      0u,    /* hop period */
                      false, /* IQ not inverted */
                      false);/* single RX */

    APP_LOG(TS_OFF, VLEVEL_M, "SubGhzPhyTask: Radio.Init done\r\n");

    /* 2. Compliance Engine — write results to g_compliance_status in SRAM2
     *    so CM4 can read duty-cycle state on every wake. */
    ComplianceEngine_Init(&g_compliance_status, HAL_GetTick);
    APP_LOG(TS_OFF, VLEVEL_M, "SubGhzPhyTask: ComplianceEngine ready\r\n");

    /* 3. Frequency Resolver — reads g_freq_resolver_state from SRAM2.
     *    CM4 populates all phases with default 868.3 MHz CELL_FREQ_STATIC at boot. */
    FrequencyResolver_Init(&g_freq_resolver_state);
    APP_LOG(TS_OFF, VLEVEL_M,
            "SubGhzPhyTask: FrequencyResolver ready, phase_count=%u\r\n",
            (unsigned)TdmaTable_PhaseCount());

    /* 4. MAC State Machine */
    MAC_Init(&s_mac_hooks);
    APP_LOG(TS_OFF, VLEVEL_M,
            "SubGhzPhyTask: MAC_Init done, state=%u clock=%u\r\n",
            (unsigned)MAC_GetState(), (unsigned)MAC_GetClockState());

    /* 5. TDMA Machine */
    static const TdmaPlatform_t plat = {
        .GetRtcMs        = plat_get_rtc_ms,
        .ProgramAlarmA   = plat_program_alarm_a,
        .RadioSetChannel = plat_radio_set_channel,
        .RadioSend       = plat_radio_send,
        .RadioSetRx      = plat_radio_set_rx,
        .RadioSleep      = plat_radio_sleep,
        .RadioTimeOnAir  = plat_radio_toa,
    };
    TdmaMachine_Init(&plat);
    APP_LOG(TS_OFF, VLEVEL_M, "SubGhzPhyTask: TdmaMachine ready\r\n");

    /* 6. Register slot task and arm first wakeup */
    UTIL_SEQ_RegTask(1u << CFG_SEQ_Task_TdmaSlotWake, 0u, TdmaMachine_SlotTask);
    UTIL_SEQ_SetTask(1u << CFG_SEQ_Task_TdmaSlotWake, CFG_SEQ_Prio_0);
    APP_LOG(TS_OFF, VLEVEL_M, "SubGhzPhyTask: sequencer task armed\r\n");
    
}
