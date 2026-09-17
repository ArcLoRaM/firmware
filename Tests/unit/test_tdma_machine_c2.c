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
static uint8_t  s_last_sent_buf[32];
static uint8_t  s_last_sent_len;
static uint32_t s_radio_set_rx_timeout;
static int      s_radio_set_rx_calls;
static int      s_radio_sleep_calls;
static uint32_t s_wait_until_ms_target;
static int      s_wait_until_ms_calls;

static uint32_t stub_GetRtcMs(void)                          { return s_rtc_ms;                                      }
static void     stub_ProgramAlarmA(uint32_t t)               { s_alarm_programmed = t; s_alarm_calls++;              }
static void     stub_RadioSetChannel(uint32_t f)             { s_channel_set = f; s_channel_calls++;                 }
static void     stub_RadioSend(const uint8_t *b, uint8_t l)
{
    if (l <= (uint8_t)sizeof(s_last_sent_buf)) {
        memcpy(s_last_sent_buf, b, l);
        s_last_sent_len = l;
    }
    s_radio_send_calls++;
}
static void     stub_RadioSetRx(uint32_t ms)                 { s_radio_set_rx_timeout = ms; s_radio_set_rx_calls++;  }
static void     stub_RadioSleep(void)                        { s_radio_sleep_calls++;                                }
static uint32_t stub_RadioTimeOnAir(void)                    { return 2500u; /* == slot_active_ms */                 }
static void     stub_WaitUntilMs(uint32_t t)                  { s_wait_until_ms_target = t; s_wait_until_ms_calls++; s_rtc_ms = t; }

static const TdmaPlatform_t k_platform = {
    .GetRtcMs       = stub_GetRtcMs,
    .ProgramAlarmA  = stub_ProgramAlarmA,
    .RadioSetChannel= stub_RadioSetChannel,
    .RadioSend      = stub_RadioSend,
    .RadioSetRx     = stub_RadioSetRx,
    .RadioSleep     = stub_RadioSleep,
    .RadioTimeOnAir = stub_RadioTimeOnAir,
    .WaitUntilMs    = stub_WaitUntilMs,
};

/* =========================================================================
 * MAC & subsystem fixtures
 * ========================================================================= */

static FrequencyResolverState_t s_freq_state;
static ComplianceStatus_t       s_comp_status;

/* Stub get_tick reuses s_rtc_ms so time advances consistently */
static uint32_t comp_get_tick(void) { return s_rtc_ms; }

/* Snapshot stub: pretend RTC reads 0 after any rtc_set */
static uint32_t s_mac_snapshot_ms;
static void stub_mac_rtc_set(uint32_t ms, uint8_t d, uint8_t mo, uint8_t y)
{
    (void)ms; (void)d; (void)mo; (void)y;
    s_mac_snapshot_ms = ms;
}
static void stub_mac_get_rtc_snapshot(uint32_t *ms, uint8_t *d, uint8_t *mo, uint8_t *y)
{
    *ms = s_mac_snapshot_ms; *d = 0x01u; *mo = 0x01u; *y = 0x24u;
}

static const MAC_Hooks_t k_mac_hooks = {
    .rtc_set           = stub_mac_rtc_set,
    .get_rtc_snapshot  = stub_mac_get_rtc_snapshot,
    .sync_bootstrapped = NULL,
    .sync_locked       = NULL,
    .sync_lost         = NULL,
};

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
    s_wait_until_ms_target = 0u;
    s_wait_until_ms_calls  = 0;
    memset(s_last_sent_buf, 0, sizeof(s_last_sent_buf));
    s_last_sent_len       = 0u;
    s_mac_snapshot_ms     = 0u;

    /* Static 868.1 MHz on Sync phase (phase 0, cell mode STATIC) */
    s_freq_state.phases[0].cell_mode           = CELL_FREQ_STATIC;
    s_freq_state.phases[0].cell_freq_or_seed = 868100000u;

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
    p.sync_phase_index = 0u;
    s_mac_snapshot_ms  = 0u;
    p.sync_cell_index = 0u;  p.ms_since_midnight_sync_phase = 0u;
    MAC_OnSyncPacketReceived(&p, 0u);
    p.sync_cell_index = 1u;
    MAC_OnSyncPacketReceived(&p, 3000u);
    p.sync_cell_index = 2u;
    MAC_OnSyncPacketReceived(&p, 6000u);
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
     * Cell 0 of Sync phase: always SLOT_RX for C2 (epoch not yet received).
     * DIRECTION_MAC_CELL + epoch not received → next_slot_is_rx=true → guard.
     * next_alarm = 0 + 2500 + 500 - GUARD_TIME_MS = 2995.
     */
    TEST_ASSERT_EQUAL(SLOT_STEP_MS - GUARD_TIME_MS, s_alarm_programmed);
}

/* =========================================================================
 * Alarm timing — RX guard (MAC Scanning → epoch not received)
 * ========================================================================= */

void test_slot_task_rx_alarm_early_by_guard(void)
{
    /* MAC in Scanning (initial): all decisions = RX; phase_tx_flag stays 0 */
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();
    /*
     * Decision = RX.  DIRECTION_MAC_CELL + epoch not received → next slot Rx.
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
    /* C2 TX requires: CLOCK_WARM + epoch received at cell 0.
     * Step 1: sync_mac → WARM. Step 2: run cell 0 (RX).
     * Step 3: arm epoch. Step 4: run cell 1 → TX → RadioSend called. */
    sync_mac();
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();        /* cell 0 → SLOT_RX, no send */
    TEST_ASSERT_EQUAL(0, s_radio_send_calls);

    /* Arm epoch: simulate receiving a sync packet in the cell-0 RX window */
    SyncPayload_t arm;
    memset(&arm, 0, sizeof(arm));
    arm.sync_phase_index             = 0u;
    arm.ms_since_midnight_sync_phase = 0u;
    arm.sync_cell_index              = 0u;
    MAC_OnSyncPacketReceived(&arm, 0u);  /* error=0 → Tier 1 → epoch armed */

    step_slot();                   /* cell 1 → SLOT_TX */
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
 * BootstrapFromSync
 * ========================================================================= */

void test_bootstrap_cursor_positioned_at_next_cell(void)
{
    /* Stub table has 3 cells per Sync phase. Bootstrap at cell=1 → advance → cell=2.
     * (cell=2 would wrap frame back to 0 since phase has only 3 cells.) */
    TdmaMachine_BootstrapFromSync(0u, 1u, 3000u);
    TEST_ASSERT_EQUAL(2u, TdmaMachine_GetCursor().cell_index);
    TEST_ASSERT_EQUAL(0u, TdmaMachine_GetCursor().phase_index);
}

void test_bootstrap_next_alarm_accounts_for_received_cell(void)
{
    /* slot_start=3000, slot_active=2500, gap=500 → nominal=6000, guard applied */
    TdmaMachine_BootstrapFromSync(0u, 1u, 3000u);
    TEST_ASSERT_EQUAL(6000u - GUARD_TIME_MS, s_alarm_programmed);
}

void test_bootstrap_cell0_cursor_at_cell1(void)
{
    /* Receive Packet 1 at cell 0, slot_start=0 → cursor must advance to cell 1. */
    TdmaMachine_BootstrapFromSync(0u, 0u, 0u);
    TEST_ASSERT_EQUAL(0u, TdmaMachine_GetCursor().phase_index);
    TEST_ASSERT_EQUAL(1u, TdmaMachine_GetCursor().cell_index);
}

void test_bootstrap_cell0_alarm_one_step(void)
{
    /* Receive at cell 0, slot_start=0 → nominal = SLOT_STEP_MS, guard applied */
    TdmaMachine_BootstrapFromSync(0u, 0u, 0u);
    TEST_ASSERT_EQUAL(SLOT_STEP_MS - GUARD_TIME_MS, s_alarm_programmed);
}

void test_bootstrap_last_cell_cursor_wraps(void)
{
    /* Receive at cell 2 (last cell of 3-cell Sync phase), slot_start=6000.
     * advance_cursor wraps to {0,0,0}: only 1 phase in table → frame wrap. */
    TdmaMachine_BootstrapFromSync(0u, 2u, 6000u);
    TEST_ASSERT_EQUAL(0u, TdmaMachine_GetCursor().phase_index);
    TEST_ASSERT_EQUAL(0u, TdmaMachine_GetCursor().cell_index);
}

void test_bootstrap_last_cell_alarm_one_step(void)
{
    /* Receive at cell 2, slot_start=6000.
     * nominal = 6000 + 3000 = 9000, guard applied */
    TdmaMachine_BootstrapFromSync(0u, 2u, 6000u);
    TEST_ASSERT_EQUAL(6000u + SLOT_STEP_MS - GUARD_TIME_MS, s_alarm_programmed);
}

void test_sync_tx_payload_fields_match_cursor(void)
{
    /* C2 can only TX at cells 1+ after receiving epoch at cell 0.
     * Sequence: sync_mac → run cell 0 (RX) → arm epoch → run cell 1 (TX). */
    sync_mac();
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();   /* cell 0 → RX, no send */

    /* Arm epoch */
    SyncPayload_t arm;
    memset(&arm, 0, sizeof(arm));
    arm.sync_phase_index             = 0u;
    arm.ms_since_midnight_sync_phase = 0u;
    arm.sync_cell_index              = 0u;
    MAC_OnSyncPacketReceived(&arm, 0u);

    step_slot();              /* cell 1 → TX */
    TEST_ASSERT_EQUAL(1, s_radio_send_calls);
    TEST_ASSERT_EQUAL(10u, s_last_sent_len);
    SyncPayload_t pkt;
    memcpy(&pkt, s_last_sent_buf, sizeof(pkt));
    TEST_ASSERT_EQUAL(0u, pkt.sync_phase_index);
    TEST_ASSERT_EQUAL(1u, pkt.sync_cell_index);  /* cursor at cell 1 when TX */
}

/* =========================================================================
 * State-aware guard: no guard when epoch received (next slot is Tx)
 * ========================================================================= */

void test_no_guard_when_epoch_received(void)
{
    sync_mac();
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();   /* cell 0 → RX, epoch not received → guard */

    /* Arm epoch: receive Tier 1 sync packet in cell-0 RX window */
    SyncPayload_t arm;
    memset(&arm, 0, sizeof(arm));
    arm.sync_phase_index             = 0u;
    arm.ms_since_midnight_sync_phase = 0u;
    arm.sync_cell_index              = 0u;
    MAC_OnSyncPacketReceived(&arm, 0u);  /* error=0 → Tier 1 → epoch armed */

    step_slot();   /* cell 1 → TX (epoch received) */
    /*
     * Alarm for cell 2: epoch received → next slot is Tx → no guard.
     * alarm = 2 × SLOT_STEP_MS = 6000 (nominal, no guard subtraction).
     */
    TEST_ASSERT_EQUAL(2u * SLOT_STEP_MS, s_alarm_programmed);
}

/* =========================================================================
 * TX delayed to nominal when woke early (guard applied + MAC decides Tx)
 * ========================================================================= */

void test_tx_delayed_to_nominal_when_woke_early(void)
{
    sync_mac();
    s_rtc_ms = 0u;
    TdmaMachine_SlotTask();   /* cell 0 → RX, alarm = SLOT_STEP_MS - GUARD */

    /* Arm epoch */
    SyncPayload_t arm;
    memset(&arm, 0, sizeof(arm));
    arm.sync_phase_index             = 0u;
    arm.ms_since_midnight_sync_phase = 0u;
    arm.sync_cell_index              = 0u;
    MAC_OnSyncPacketReceived(&arm, 0u);

    step_slot();   /* cell 1: woke early (guard), MAC decides Tx */
    /*
     * WaitUntilMs must be called with nominal slot start (3000).
     * The stub advances s_rtc_ms to 3000.
     */
    TEST_ASSERT_EQUAL(1, s_wait_until_ms_calls);
    TEST_ASSERT_EQUAL(SLOT_STEP_MS, s_wait_until_ms_target);
    TEST_ASSERT_EQUAL(1, s_radio_send_calls);
}

/* =========================================================================
 * Bootstrap applies guard time on first alarm
 * ========================================================================= */

void test_bootstrap_applies_guard_on_alarm(void)
{
    /* After receiving Packet 1 at cell 0 (slot_start=0), the next alarm must
     * subtract guard: alarm = SLOT_STEP_MS - GUARD_TIME_MS. */
    TdmaMachine_BootstrapFromSync(0u, 0u, 0u);
    TEST_ASSERT_EQUAL(SLOT_STEP_MS - GUARD_TIME_MS, s_alarm_programmed);
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
    RUN_TEST(test_bootstrap_cursor_positioned_at_next_cell);
    RUN_TEST(test_bootstrap_next_alarm_accounts_for_received_cell);
    RUN_TEST(test_bootstrap_cell0_cursor_at_cell1);
    RUN_TEST(test_bootstrap_cell0_alarm_one_step);
    RUN_TEST(test_bootstrap_last_cell_cursor_wraps);
    RUN_TEST(test_bootstrap_last_cell_alarm_one_step);
    RUN_TEST(test_sync_tx_payload_fields_match_cursor);

/* ------- State-aware guard ----------------------------------------------- */

    RUN_TEST(test_no_guard_when_epoch_received);

/* ------- TX delay to nominal -------------------------------------------- */

    RUN_TEST(test_tx_delayed_to_nominal_when_woke_early);
/* ------- Bootstrap guard ----------------------------------------------- */

    RUN_TEST(test_bootstrap_applies_guard_on_alarm);
    return UNITY_END();
}
