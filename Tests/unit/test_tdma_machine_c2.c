#include "unity.h"
#include "tdma_machine.h"
#include "mac_state_machine.h"
#include "freq_resolver.h"
#include "compliance_engine.h"
#include "shared_mem.h"
#include "tdma_table.h"
#include <string.h>
#include <stdint.h>

/* =========================================================================
 * Platform stubs
 * ========================================================================= */

static uint32_t s_rtc_ms;
static uint32_t s_alarm_programmed;
static int      s_alarm_calls;
static uint32_t s_channel_set;
static int      s_channel_calls;
static int      s_radio_send_calls;
static uint32_t s_radio_set_rx_timeout;
static int      s_radio_set_rx_calls;
static int      s_radio_sleep_calls;

static uint32_t stub_GetRtcMs(void)                          { return s_rtc_ms;                                      }
static void     stub_ProgramAlarmA(uint32_t t)               { s_alarm_programmed = t; s_alarm_calls++;              }
static void     stub_RadioSetChannel(uint32_t f)             { s_channel_set = f; s_channel_calls++;                 }
static void     stub_RadioSend(const uint8_t *b, uint8_t l)  { (void)b; (void)l; s_radio_send_calls++;              }
static void     stub_RadioSetRx(uint32_t ms)                 { s_radio_set_rx_timeout = ms; s_radio_set_rx_calls++;  }
static void     stub_RadioSleep(void)                        { s_radio_sleep_calls++;                                }
static uint32_t stub_RadioTimeOnAir(void)                    { return 2500u; /* == slot_active_ms */                 }

static const TdmaPlatform_t k_platform = {
    .GetRtcMs       = stub_GetRtcMs,
    .ProgramAlarmA  = stub_ProgramAlarmA,
    .RadioSetChannel= stub_RadioSetChannel,
    .RadioSend      = stub_RadioSend,
    .RadioSetRx     = stub_RadioSetRx,
    .RadioSleep     = stub_RadioSleep,
    .RadioTimeOnAir = stub_RadioTimeOnAir,
};

/* =========================================================================
 * MAC & subsystem fixtures
 * ========================================================================= */

static FrequencyResolverState_t s_freq_state;
static ComplianceStatus_t       s_comp_status;

/* Stub get_tick reuses s_rtc_ms so time advances consistently */
static uint32_t comp_get_tick(void) { return s_rtc_ms; }

static const MAC_Hooks_t k_mac_hooks = { NULL, NULL, NULL, NULL };

/* Stub TDMA: slot_active_ms=2500, gap[0]=500, per-slot step=3000 ms */
#define SLOT_ACTIVE_MS   2500u
#define GAP_MS            500u
#define SLOT_STEP_MS     3000u   /* SLOT_ACTIVE_MS + GAP_MS */

static void init_all(void)
{
    memset(&s_freq_state,  0, sizeof(s_freq_state));
    memset(&s_comp_status, 0, sizeof(s_comp_status));
    s_rtc_ms = 0u;
    s_alarm_programmed    = 0u;
    s_alarm_calls         = 0;
    s_channel_set         = 0u;
    s_channel_calls       = 0;
    s_radio_send_calls    = 0;
    s_radio_set_rx_timeout= 0u;
    s_radio_set_rx_calls  = 0;
    s_radio_sleep_calls   = 0;

    /* Static 868.1 MHz on Sync phase (phase 0, cell mode STATIC) */
    s_freq_state.phases[0].cell_mode           = CELL_FREQ_STATIC;
    s_freq_state.phases[0].cell.static_freq_hz = 868100000u;

    FrequencyResolver_Init(&s_freq_state);
    ComplianceEngine_Init(&s_comp_status, comp_get_tick);
    MAC_Init(&k_mac_hooks);
    TdmaMachine_Init(&k_platform);
}

/* Drive MAC to Synchronized state (CLOCK_WARM) using the stub table timings */
static void sync_mac(void)
{
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_slot_index = 0u;  MAC_OnSyncPacketReceived(&p, 0u);
    p.sync_slot_index = 1u;  MAC_OnSyncPacketReceived(&p, 3000u);
    p.sync_slot_index = 2u;  MAC_OnSyncPacketReceived(&p, 6000u);
}

/* Call SlotTask after advancing simulated RTC to the last programmed alarm */
static void step_slot(void)
{
    s_rtc_ms = s_alarm_programmed;
    TdmaMachine_SlotTask();
}

void setUp(void)    { init_all(); }
void tearDown(void) {}

/* =========================================================================
 * Init
 * ========================================================================= */

void test_init_sets_cursor_zero(void)
{
    FrameCursor_t c = TdmaMachine_GetCursor();
    TEST_ASSERT_EQUAL(0u, c.phase_index);
    TEST_ASSERT_EQUAL(0u, c.cell_index);
    TEST_ASSERT_EQUAL(0u, c.slot_index);
}

/* =========================================================================
 * Alarm timing — nominal TX path (MAC Synchronized, phase_tx_flag=1)
 * ========================================================================= */

void test_slot_task_programs_alarm_nominal_for_tx_slot(void)
{
    sync_mac();                       /* MAC → Synchronized */
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();
    /*
     * MAC Synchronized, first Sync phase entry → audit_cycle=0 (even)
     * → phase_tx_flag=1 → TX.  Next slot also TX (same direction) → no guard.
     * next_alarm = 0 + 2500 + 500 = 3000.
     */
    TEST_ASSERT_EQUAL(SLOT_STEP_MS, s_alarm_programmed);
}

/* =========================================================================
 * Alarm timing — RX guard (MAC Scanning → phase_tx_flag=0)
 * ========================================================================= */

void test_slot_task_rx_alarm_early_by_guard(void)
{
    /* MAC in Scanning (initial): all decisions = RX; phase_tx_flag stays 0 */
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();
    /*
     * Decision = RX.  DIRECTION_MAC_PHASE + phase_tx_flag=0 → next slot RX.
     * alarm = 0 + SLOT_STEP_MS - GUARD_TIME_MS = 3000 - 5 = 2995.
     */
    TEST_ASSERT_EQUAL(SLOT_STEP_MS - GUARD_TIME_MS, s_alarm_programmed);
}

void test_slot_task_rx_sets_correct_radio_timeout(void)
{
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();
    /* RadioSetRx(slot_active_ms + 2×GUARD_TIME_MS) = 2500 + 10 = 2510 */
    TEST_ASSERT_EQUAL(1, s_radio_set_rx_calls);
    TEST_ASSERT_EQUAL(SLOT_ACTIVE_MS + 2u * GUARD_TIME_MS, s_radio_set_rx_timeout);
}

/* =========================================================================
 * TX path — RadioSend called, compliance grants
 * ========================================================================= */

void test_slot_task_tx_calls_radio_send(void)
{
    sync_mac();   /* MAC → Synchronized, phase_tx_flag will be 1 on first entry */
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();
    TEST_ASSERT_EQUAL(1, s_radio_send_calls);
}

/* =========================================================================
 * Compliance — RESTRICTED → RadioSend skipped, alarm still programmed
 * ========================================================================= */

void test_slot_task_compliance_skip_no_radio_send(void)
{
    sync_mac();
    /* Exhaust all compliance credit before SlotTask runs */
    ComplianceEngine_RequestChannel(868100000u, 36000u, 14);

    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();

    TEST_ASSERT_EQUAL(0, s_radio_send_calls);
}

void test_slot_task_compliance_skip_programs_alarm(void)
{
    sync_mac();
    ComplianceEngine_RequestChannel(868100000u, 36000u, 14);

    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();

    TEST_ASSERT_GREATER_THAN(0, s_alarm_calls);
}

/* =========================================================================
 * RadioSetChannel called with FrequencyResolver result
 * ========================================================================= */

void test_slot_task_calls_radio_set_channel(void)
{
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();
    TEST_ASSERT_EQUAL(1,           s_channel_calls);
    TEST_ASSERT_EQUAL(868100000u,  s_channel_set);
}

/* =========================================================================
 * Cursor advancement across three slots
 * ========================================================================= */

void test_cursor_advances_cell_index(void)
{
    /* Slot 0 → cell_index becomes 1 */
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();
    TEST_ASSERT_EQUAL(1u, TdmaMachine_GetCursor().cell_index);

    /* Slot 1 → cell_index becomes 2 */
    step_slot();
    TEST_ASSERT_EQUAL(2u, TdmaMachine_GetCursor().cell_index);
}

void test_frame_wrap_resets_cursor(void)
{
    /* Three SlotTask calls exhaust the 3-cell Sync phase → frame wrap */
    s_rtc_ms = 0u; TdmaMachine_SlotTask();
    step_slot();
    step_slot();

    FrameCursor_t c = TdmaMachine_GetCursor();
    TEST_ASSERT_EQUAL(0u, c.phase_index);
    TEST_ASSERT_EQUAL(0u, c.cell_index);
    TEST_ASSERT_EQUAL(0u, c.slot_index);
}

/* =========================================================================
 * Cursor integrity checkpoint
 * ========================================================================= */

void test_cursor_integrity_clean_on_expected_wake(void)
{
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();                /* programs alarm, stores expected wake */

    s_rtc_ms = s_alarm_programmed;         /* wake exactly on time */
    TdmaMachine_SlotTask();

    TEST_ASSERT_FALSE(TdmaMachine_IsCursorSuspect());
}

void test_cursor_suspect_on_implausible_delta(void)
{
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();                /* expected_wake = s_alarm_programmed */

    /* Arrive 4000 ms late: 4000 > 1.5 × 2500 = 3750 → suspect */
    s_rtc_ms = s_alarm_programmed + 4000u;
    TdmaMachine_SlotTask();

    TEST_ASSERT_TRUE(TdmaMachine_IsCursorSuspect());
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_cursor_zero);
    RUN_TEST(test_slot_task_programs_alarm_nominal_for_tx_slot);
    RUN_TEST(test_slot_task_rx_alarm_early_by_guard);
    RUN_TEST(test_slot_task_rx_sets_correct_radio_timeout);
    RUN_TEST(test_slot_task_tx_calls_radio_send);
    RUN_TEST(test_slot_task_compliance_skip_no_radio_send);
    RUN_TEST(test_slot_task_compliance_skip_programs_alarm);
    RUN_TEST(test_slot_task_calls_radio_set_channel);
    RUN_TEST(test_cursor_advances_cell_index);
    RUN_TEST(test_frame_wrap_resets_cursor);
    RUN_TEST(test_cursor_integrity_clean_on_expected_wake);
    RUN_TEST(test_cursor_suspect_on_implausible_delta);
    return UNITY_END();
}
