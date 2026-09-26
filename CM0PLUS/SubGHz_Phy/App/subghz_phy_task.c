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
#include "arclog.h"          /* ARCLOG, VLEVEL_* */
#include "app_version.h"      /* APP_VERSION_* */
#include "stm32_timer.h"      /* UTIL_TIMER_GetCurrentTime */
#include "radio.h"            /* Radio */
#include "radio_def.h"        /* MODEM_LORA */
#include "radio_driver.h"     /* SUBGRF_SetDioIrqParams, SUBGRF_GetIrqStatus, IRQ_* */
#include "rtc.h"              /* hrtc */
#include "main.h"             /* RTC_PREDIV_S */
#include "tdma_machine.h"
#include "mac_state_machine.h"
#include "freq_resolver.h"
#include "compliance_engine.h"
#include "shared_mem.h"
#include "protocol_types.h"   /* NODE_CLASS_C* constants */
#include "tdma_table.h"       /* TdmaTable_PhaseCount */
#include "guard_time_resolver.h" /* MAX_GUARD_TIME_MS */

/* =========================================================================
 * Radio IRQ timestamps
 *
 * The radio IRQ handler (stm32wlxx_it.c, USER CODE SUBGHZ_Radio_IRQn 0) calls
 * SubGhzPhyTask_OnRadioIrq() first thing, before HAL dispatch, so every stamp
 * is the RTC at IRQ entry. The Sync timestamp given to the MAC (SyncStamp) is
 * the packet's start on air: RxDone minus the airtime of the received length.
 * Both are deterministic, so it tracks the sender's TX start to within an RTC
 * tick. The PREAMBLE_DETECTED IRQ is not: at SF12 it lands one symbol
 * (32.8 ms) early or late from packet to packet (bench, 2026-09-26). It is
 * still stamped, with HEADER_VALID, as a diagnostic in RX_DONE.
 * ========================================================================= */

#ifndef RX_DONE_LATENCY_MS
/* Delay between the end of the packet on air and the RxDone IRQ stamp,
 * subtracted from the SyncStamp. The radio raises RxDone right after the
 * last symbol and the IRQ is stamped at entry, so it is well under a
 * millisecond; 0 until a common time reference (e.g. GPIO on a logic
 * analyser) measures it. */
#define RX_DONE_LATENCY_MS  0u
#endif

/* Time the radio may take to raise PREAMBLE_DETECTED after a packet starts,
 * added to a synced window's latest packet start to form the hardware Rx
 * timeout. Detection happens within the programmed preamble, so its length
 * (8 symbols x 32.768 ms at SF12/BW125 = 262 ms) is a safe upper bound. A
 * packet detected in that margin but starting too late to fit is aborted by
 * the cap. Recompute if the modem parameters change. */
#define RX_PREAMBLE_DETECT_MARGIN_MS  262u

/* Largest hardware Rx timeout: 24-bit count of 15.625 us steps. */
#define RX_HW_TIMEOUT_MAX_MS  (0xFFFFFFu >> 6)

static RadioEvents_t     s_radio_events;
static UTIL_TIMER_Object_t s_rx_cap_timer;   /* synced-window hard end */
static volatile uint32_t s_irq_stamp_ms;   /* RTC at entry of the latest radio IRQ */
static volatile uint32_t s_pre_stamp_ms;   /* PREAMBLE_DETECTED of the current Rx */
static volatile uint32_t s_hdr_stamp_ms;   /* HEADER_VALID of the current Rx      */
static volatile bool     s_pre_valid;
static volatile bool     s_hdr_valid;
static uint8_t           s_last_tx_len;

static uint32_t plat_radio_toa(uint8_t len);

void SubGhzPhyTask_OnRadioIrq(void)
{
    uint32_t stamp = (uint32_t)UTIL_TIMER_GetCurrentTime();
    uint16_t irq   = SUBGRF_GetIrqStatus();

    /* Keep the latest detection: a false preamble detection on noise earlier
     * in the window is superseded by the real packet's. */
    s_irq_stamp_ms = stamp;
    if ((irq & IRQ_PREAMBLE_DETECTED) != 0u) {
        s_pre_stamp_ms = stamp;
        s_pre_valid    = true;
        s_hdr_valid    = false;
    }
    if ((irq & IRQ_HEADER_VALID) != 0u) {
        s_hdr_stamp_ms = stamp;
        s_hdr_valid    = true;
    }
}

/* =========================================================================
 * Radio event callbacks - bridge radio ISR events to the MAC layer
 * ========================================================================= */

static void on_tx_done(void)
{
    uint32_t end = s_irq_stamp_ms;
    uint32_t toa = plat_radio_toa(s_last_tx_len);
    /* start = end - toa: when the radio actually began transmitting. */
    ARCLOG(ARCLOG_MOD_RADIO, VLEVEL_M, "TX_DONE", "sz=%u toa=%u end=%u start=%u",
           (unsigned)s_last_tx_len, (unsigned)toa,
           (unsigned)end, (unsigned)(end - toa));
}

static void on_tx_timeout(void)
{
    ARCLOG(ARCLOG_MOD_RADIO, VLEVEL_L, "TX_TIMEOUT", "");
}

static void on_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    uint32_t rxd   = s_irq_stamp_ms;
    uint32_t toa   = plat_radio_toa((uint8_t)size);
    uint32_t stamp = rxd - toa - RX_DONE_LATENCY_MS;   /* SyncStamp: packet start */

    UTIL_TIMER_Stop(&s_rx_cap_timer);
    ARCLOG(ARCLOG_MOD_RADIO, VLEVEL_M, "RX_DONE",
           "sz=%u rssi=%d snr=%d pre=%u hdr=%u rxd=%u toa=%u st=%u",
           (unsigned)size, (int)rssi, (int)snr,
           (unsigned)(s_pre_valid ? s_pre_stamp_ms : 0u),
           (unsigned)(s_hdr_valid ? s_hdr_stamp_ms : 0u),
           (unsigned)rxd, (unsigned)toa, (unsigned)stamp);

    if (size == (uint16_t)sizeof(SyncPayload_t)) {
        MAC_OnSyncPacketReceived((const SyncPayload_t *)payload, stamp);
    } else if (size == (uint16_t)sizeof(BeaconPayload_t)) {
        MAC_OnBeaconReceived((const BeaconPayload_t *)payload);
    }
    TdmaMachine_OnRxEnd();
}

static void on_rx_timeout(void)
{
    UTIL_TIMER_Stop(&s_rx_cap_timer);
    ARCLOG(ARCLOG_MOD_RADIO, VLEVEL_H, "RX_TIMEOUT", "pre=%u", (unsigned)s_pre_valid);
    TdmaMachine_OnRxEnd();
}

static void on_rx_error(void)
{
    UTIL_TIMER_Stop(&s_rx_cap_timer);
    ARCLOG(ARCLOG_MOD_RADIO, VLEVEL_M, "RX_ERROR", "pre=%u hdr=%u",
           (unsigned)s_pre_valid, (unsigned)s_hdr_valid);
    TdmaMachine_OnRxEnd();
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
static void plat_radio_send(const uint8_t *b, uint8_t l)
{
    s_last_tx_len = l;
    Radio.Send((uint8_t *)b, l);
}

/* Start a single-mode Rx with the radio's own window timer (steps of
 * 15.625 us; 0 = no timeout). Radio.Rx() is bypassed: it can only bound the
 * window with the driver's software RxTimeoutTimer, which is stopped by
 * RxDone / errors only and so cuts a packet that is still arriving. The
 * hardware timer instead stops on preamble detection, so a packet that
 * started in time is always received in full. Preamble rather than header
 * (SetRxConfig's default): it is the earliest proof of a packet, and the
 * Sync packet will move to implicit header (issue #39 keeps the tradeoff).
 * It is re-applied on every start rather than trusted to survive the radio's
 * sleep between slots. The IRQ mask adds PREAMBLE_DETECTED and HEADER_VALID so the IRQ
 * handler can stamp them; RadioIrqProcess handles both harmlessly (its
 * preamble branch only acts in Rx duty-cycle mode, never used here). */
static void radio_rx_start(uint32_t hw_timeout_steps)
{
    const uint16_t mask = IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT | IRQ_CRC_ERROR
                        | IRQ_HEADER_ERROR | IRQ_PREAMBLE_DETECTED | IRQ_HEADER_VALID;
    s_pre_valid = false;
    s_hdr_valid = false;
    SUBGRF_SetDioIrqParams(mask, mask, IRQ_RADIO_NONE, IRQ_RADIO_NONE);
    SUBGRF_SetStopRxTimerOnPreambleDetect(true);
    SUBGRF_SetSwitch(RFO_LP, RFSWITCH_RX);  /* PA selection is ignored for Rx */
    SUBGRF_SetRx(hw_timeout_steps);
}

/* Synced window: the hardware timeout is the latest packet start plus the
 * time the radio needs to detect its preamble; the cap then aborts any
 * reception still running at the slot's hard end (TdmaPlatform_t). */
static void plat_radio_set_rx(uint32_t start_window_ms, uint32_t cap_ms)
{
    uint32_t tmo_ms = start_window_ms + RX_PREAMBLE_DETECT_MARGIN_MS;
    if (tmo_ms > RX_HW_TIMEOUT_MAX_MS) {
        tmo_ms = RX_HW_TIMEOUT_MAX_MS;
    }
    radio_rx_start(tmo_ms << 6);  /* 64 steps of 15.625 us per ms */
    UTIL_TIMER_StartWithPeriod(&s_rx_cap_timer, cap_ms);
}

/* Scanning: listen until a reception ends; TdmaMachine_OnRxEnd re-arms. */
static void plat_radio_scan(void)
{
    radio_rx_start(0u);
}

/* The slot's hard end passed with a reception still running: a false
 * preamble detection, or a packet that started too late to end in time. */
static void on_rx_cap(void *context)
{
    (void)context;
    if (SUBGRF_GetOperatingMode() != MODE_RX) return;
    ARCLOG(ARCLOG_MOD_RADIO, VLEVEL_L, "RX_CAP", "pre=%u hdr=%u",
           (unsigned)(s_pre_valid ? s_pre_stamp_ms : 0u),
           (unsigned)(s_hdr_valid ? s_hdr_stamp_ms : 0u));
    TdmaMachine_OnRxEnd();  /* puts the radio to sleep */
}

/* Block until the RTC reaches abs_ms. The TDMA Machine calls it only after
 * a guarded (early) wake in a slot the MAC then made Tx, so the wait is at
 * most one guard time; a target already passed, or further away than that
 * (not a guarded wake), returns at once. Normally unused: the next wake is
 * re-decided at Rx end (TdmaMachine_OnRxEnd), so a Tx slot is not woken
 * early. Midnight-safe. */
static void plat_wait_until_ms(uint32_t abs_ms)
{
    for (;;) {
        uint32_t left = (abs_ms % MS_PER_DAY + MS_PER_DAY - plat_get_rtc_ms()) % MS_PER_DAY;
        if (left == 0u || left > MAX_GUARD_TIME_MS) {
            return;
        }
    }
}

static void plat_cancel_alarm_a(void)
{
    hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
    HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
}
static void     plat_radio_sleep(void)                         { Radio.Sleep();                     }

/* Time on air of a len-byte packet with the modem configuration set in
 * SubGhzPhyTask_Init (SF12 / BW125 / CR4-5 / 8-symbol preamble / explicit
 * header / CRC on). The driver's formula is the reference for duty-cycle
 * accounting and for deriving TX start (TX_DONE) and the SyncStamp
 * (RxDone − ToA). Keep the parameters in step with Radio.SetTxConfig/SetRxConfig. */
static uint32_t plat_radio_toa(uint8_t len)
{
    return Radio.TimeOnAir(MODEM_LORA,
                           0u,     /* BW 125 kHz  */
                           12u,    /* SF12        */
                           1u,     /* CR 4/5      */
                           8u,     /* preamble symbols */
                           false,  /* variable length (explicit header) */
                           len,
                           true);  /* CRC on */
}

/* =========================================================================
 * MAC State Machine hooks
 * ========================================================================= */

/* Packet 1 / Tier 3 hook: decompose binary target_ms to H:M:S; apply HAL_RTC_SetTime(BIN).
 * SetTime starts the second at target_s.000; the sub-second component
 * (target_ms % 1000) is then added by a SHIFTR advance (ADD1S=1 with SUBFS). */
static void mac_hook_rtc_set(uint32_t target_ms,
                              uint8_t  day, uint8_t month, uint8_t year)
{
    uint32_t old_ms    = (uint32_t)UTIL_TIMER_GetCurrentTime();
    uint32_t target_s  = (target_ms / 1000u) % 86400u;
    uint32_t subsec_ms = target_ms % 1000u;

    RTC_TimeTypeDef t = {0};
    t.Hours          = (uint8_t)(target_s / 3600u);
    t.Minutes        = (uint8_t)((target_s % 3600u) / 60u);
    t.Seconds        = (uint8_t)(target_s % 60u);
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;

    RTC_DateTypeDef d = {0};
    d.Date  = day;
    d.Month = month;
    d.Year  = year;

    hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
    HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BCD);
    /* SSR = PREDIV_S (start of target_s) after SetTime */

    /* Sub-second alignment via SHIFTR (RM0453, RTC_SHIFTR): ADD1S adds one
     * second and SUBFS is added to the SSR down-counter, delaying the clock
     * by SUBFS/(PREDIV_S+1). The net advance is 1 − SUBFS/(PREDIV_S+1) =
     * shift_ticks/(PREDIV_S+1) ≈ subsec_ms/1000, never a whole second, so
     * no date roll-over. Until SSR counts back below PREDIV_S the calendar
     * reads target_s + 1 with SSR > PREDIV_S; the time readers in
     * timer_if.c borrow that second back. */
    const char *shift = "none";
    if (subsec_ms > 0u) {
        hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
        shift = "busy";   /* a previous shift is still pending (SHPF) */
        if (!READ_BIT(hrtc.Instance->ICSR, RTC_ICSR_SHPF)) {
            uint32_t shift_ticks = (subsec_ms * (RTC_PREDIV_S + 1u)) / 1000u;
            shift = (HAL_RTCEx_SetSynchroShift(&hrtc, RTC_SHIFTADD1S_SET,
                                                (RTC_PREDIV_S + 1u) - shift_ticks) == HAL_OK)
                    ? "ok" : "fail";
        }
    }

    /* d = jump applied to the local clock (new - old domain), in ms. */
    ARCLOG(ARCLOG_MOD_SYNC, VLEVEL_L, "RTC_SET",
           "old=%u new=%u d=%d date=%02x%02x%02x shift=%s",
           (unsigned)old_ms, (unsigned)target_ms,
           (int)(int32_t)(target_ms - old_ms),
           (unsigned)year, (unsigned)month, (unsigned)day, shift);
}

/* Tier 2 hook: apply SSR-only correction when CLOCK_WARM and
 * SYNC_PARTICIPATE_THRESHOLD_MS (8 ms) <= error < SYNC_RESYNC_THRESHOLD_MS (= MAX_GUARD_TIME_MS).
 * Direction: error_ms > 0 → RTC fast → delay (ADD1S=0).
 *            error_ms < 0 → RTC slow → advance (ADD1S=1, SSR set to SUBFS). */
static void mac_hook_rtc_align_sub(uint32_t stamp_ms,
                                    uint32_t expected_offset_ms)
{
    int32_t error_ms = (int32_t)stamp_ms - (int32_t)expected_offset_ms;

    if (error_ms == 0) return;

    hrtc.IsEnabled.RtcFeatures = UINT32_MAX;

    uint32_t error_abs   = (error_ms < 0) ? (uint32_t)(-error_ms) : (uint32_t)(error_ms);
    uint32_t shift_ticks = (error_abs * (RTC_PREDIV_S + 1u)) / 1000u;

    HAL_StatusTypeDef st;
    if (error_ms > 0) {
        /* RTC fast → delay: SUBFS added to SSR prescaler counter */
        st = HAL_RTCEx_SetSynchroShift(&hrtc, RTC_SHIFTADD1S_RESET, shift_ticks);
    } else {
        /* RTC slow → advance: ADD1S=1 sets SSR = SUBFS after +1 calendar second.
         * Advance = 1 − SUBFS/(PREDIV_S+1) = shift_ticks/(PREDIV_S+1)  (AN4759 §2.6) */
        st = HAL_RTCEx_SetSynchroShift(&hrtc, RTC_SHIFTADD1S_SET,
                                        (RTC_PREDIV_S + 1u) - shift_ticks);
    }

    /* err > 0: local clock was ahead and is delayed; ticks of 1/(PREDIV_S+1) s. */
    ARCLOG(ARCLOG_MOD_SYNC, VLEVEL_M, "RTC_SHIFT", "err=%d ticks=%u res=%s",
           (int)error_ms, (unsigned)shift_ticks, (st == HAL_OK) ? "ok" : "fail");
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
 * to the cell the Sync packet was actually received in. Uses the
 * schedule-derived nominal cell start (not a hardware readback) as the
 * alarm base so all nodes wake at the same absolute slot boundary. */
static void mac_hook_sync_bootstrapped(uint8_t  sync_phase_idx,
                                        uint8_t  sync_cell_idx,
                                        uint32_t nominal_start_ms)
{
    ARCLOG(ARCLOG_MOD_TDMA, VLEVEL_M, "BOOTSTRAP", "ph=%u ce=%u nom=%u",
           (unsigned)sync_phase_idx, (unsigned)sync_cell_idx,
           (unsigned)nominal_start_ms);
    TdmaMachine_BootstrapFromSync(sync_phase_idx, sync_cell_idx, nominal_start_ms);
}

/* ClockState transitions are logged by the MAC itself (CLK events). */

static const MAC_Hooks_t s_mac_hooks = {
    .rtc_set             = mac_hook_rtc_set,
    .rtc_align_subsecond = mac_hook_rtc_align_sub,
    .get_rtc_snapshot    = mac_hook_get_rtc_snapshot,
    .sync_bootstrapped   = mac_hook_sync_bootstrapped,
    .sync_locked         = NULL,
    .sync_lost           = NULL,
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

    ARCLOG(ARCLOG_MOD_SYS, VLEVEL_ALWAYS, "BOOT", "cls=C%u fw=%u.%u.%u",
           (unsigned)NODE_CLASS, (unsigned)APP_VERSION_MAIN,
           (unsigned)APP_VERSION_SUB1, (unsigned)APP_VERSION_SUB2);

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
                      0u,    /* symbol timeout: off, the window timer bounds Rx */
                      false, /* fixed length */
                      0u,    /* payload length (variable) */
                      true,  /* CRC on */
                      false, /* FHSS off */
                      0u,    /* hop period */
                      false, /* IQ not inverted */
                      false);/* single RX */

    UTIL_TIMER_Create(&s_rx_cap_timer, 0xFFFFFFFFu, UTIL_TIMER_ONESHOT,
                      on_rx_cap, NULL);

    /* 2. Compliance Engine — write results to g_compliance_status in SRAM2
     *    so CM4 can read duty-cycle state on every wake. */
    ComplianceEngine_Init(&g_compliance_status, HAL_GetTick);

    /* 3. Frequency Resolver — reads g_freq_resolver_state from SRAM2.
     *    CM4 populates all phases with default 868.3 MHz CELL_FREQ_STATIC at boot. */
    FrequencyResolver_Init(&g_freq_resolver_state);

    /* 4. MAC State Machine */
    MAC_Init(&s_mac_hooks);   /* logs MAC_INIT */

    /* 5. TDMA Machine */
    static const TdmaPlatform_t plat = {
        .GetRtcMs        = plat_get_rtc_ms,
        .ProgramAlarmA   = plat_program_alarm_a,
        .CancelAlarmA    = plat_cancel_alarm_a,
        .RadioSetChannel = plat_radio_set_channel,
        .RadioSend       = plat_radio_send,
        .RadioSetRx      = plat_radio_set_rx,
        .RadioScan       = plat_radio_scan,
        .RadioSleep      = plat_radio_sleep,
        .RadioTimeOnAir  = plat_radio_toa,
        .WaitUntilMs     = plat_wait_until_ms,
    };
    TdmaMachine_Init(&plat);

    /* 6. Register the slot task. C1/C2 boot cold and scan (no alarm chain
     *    until the first Sync packet); C3 runs its first slot now. */
    UTIL_SEQ_RegTask(1u << CFG_SEQ_Task_TdmaSlotWake, 0u, TdmaMachine_SlotTask);
    if (TdmaMachine_Start()) {
        UTIL_SEQ_SetTask(1u << CFG_SEQ_Task_TdmaSlotWake, CFG_SEQ_Prio_0);
    }
    ARCLOG(ARCLOG_MOD_SYS, VLEVEL_L, "INIT_DONE", "phases=%u",
           (unsigned)TdmaTable_PhaseCount());
}
