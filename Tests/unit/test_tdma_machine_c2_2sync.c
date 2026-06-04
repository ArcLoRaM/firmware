#include "unity.h"
#include "tdma_machine.h"
#include "mac_state_machine.h"
#include "freq_resolver.h"
#include "compliance_engine.h"
#include "shared_mem.h"
#include "tdma_table.h"
#include <string.h>
#include <stdint.h>

/*
 * Two-Sync-phase frame: Sync0 (phase 0, cells 0-2) + Sync1 (phase 1, cells 0-2).
 * per_cell_ms = 2500 + 500 = 3000.  Sync1 starts at 9000 ms.  frame_duration = 18000 ms.
 *
 * These tests verify the "one step forward" bootstrap invariant in a frame that
 * contains two Sync phases.  The key assertion for the last-cell case is
 * alarm == 9000, which distinguishes the correct formula from two wrong ones:
 *   Wrong (Frame Epoch + frame_duration)  : 0 + 18000 = 18000
 *   Wrong (slot_start  + frame_duration)  : 6000 + 18000 = 24000
 */

/* =========================================================================
 * Platform stubs
 * ========================================================================= */

static uint32_t s_rtc_ms;
static uint32_t s_alarm_programmed;
static int      s_alarm_calls;
static uint32_t s_channel_set;
static int      s_channel_calls;
static int      s_radio_send_calls;
static int      s_radio_set_rx_calls;
static int      s_radio_sleep_calls;

static uint32_t stub_GetRtcMs(void)                          { return s_rtc_ms;                              }
static void     stub_ProgramAlarmA(uint32_t t)               { s_alarm_programmed = t; s_alarm_calls++;      }
static void     stub_RadioSetChannel(uint32_t f)             { s_channel_set = f; s_channel_calls++;         }
static void     stub_RadioSend(const uint8_t *b, uint8_t l)  { (void)b; (void)l; s_radio_send_calls++;      }
static void     stub_RadioSetRx(uint32_t ms)                 { (void)ms; s_radio_set_rx_calls++;             }
static void     stub_RadioSleep(void)                        { s_radio_sleep_calls++;                        }
static uint32_t stub_RadioTimeOnAir(void)                    { return 2500u;                                 }

static const TdmaPlatform_t k_platform = {
    .GetRtcMs        = stub_GetRtcMs,
    .ProgramAlarmA   = stub_ProgramAlarmA,
    .RadioSetChannel = stub_RadioSetChannel,
    .RadioSend       = stub_RadioSend,
    .RadioSetRx      = stub_RadioSetRx,
    .RadioSleep      = stub_RadioSleep,
    .RadioTimeOnAir  = stub_RadioTimeOnAir,
};

/* =========================================================================
 * MAC & subsystem fixtures
 * ========================================================================= */

static FrequencyResolverState_t s_freq_state;
static ComplianceStatus_t       s_comp_status;

static uint32_t comp_get_tick(void) { return s_rtc_ms; }

static uint32_t s_mac_snapshot_ms;
static void stub_mac_rtc_set(uint32_t ms, uint8_t d, uint8_t mo, uint8_t y)
{
    (void)d; (void)mo; (void)y;
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

/* Stub table constants (must match stub_tdma_table_2sync.c) */
#define SLOT_ACTIVE_MS    2500u
#define GAP_MS             500u
#define SLOT_STEP_MS      3000u   /* SLOT_ACTIVE_MS + GAP_MS */
#define SYNC1_START_MS    9000u   /* phase_start_ms[1] */
#define FRAME_DURATION_MS 18000u  /* 2 phases × 3 cells × 3000 ms */

static void init_all(void)
{
    memset(&s_freq_state,  0, sizeof(s_freq_state));
    memset(&s_comp_status, 0, sizeof(s_comp_status));
    s_rtc_ms            = 0u;
    s_alarm_programmed  = 0u;
    s_alarm_calls       = 0;
    s_channel_set       = 0u;
    s_channel_calls     = 0;
    s_radio_send_calls  = 0;
    s_radio_set_rx_calls= 0;
    s_radio_sleep_calls = 0;
    s_mac_snapshot_ms   = 0u;

    s_freq_state.phases[0].cell_mode           = CELL_FREQ_STATIC;
    s_freq_state.phases[0].cell.static_freq_hz = 868100000u;
    s_freq_state.phases[1].cell_mode           = CELL_FREQ_STATIC;
    s_freq_state.phases[1].cell.static_freq_hz = 868100000u;

    FrequencyResolver_Init(&s_freq_state);
    ComplianceEngine_Init(&s_comp_status, comp_get_tick);
    MAC_Init(&k_mac_hooks);
    TdmaMachine_Init(&k_platform);
}

static void step_slot(void)
{
    s_rtc_ms = s_alarm_programmed;
    TdmaMachine_SlotTask();
}

void setUp(void)    { init_all(); }
void tearDown(void) {}

/* =========================================================================
 * Baseline — mid-cell of Sync0 (same behaviour as single-phase table)
 * ========================================================================= */

void test_2sync_bootstrap_mid_cell_alarm(void)
{
    /* Receive at cell 1 of Sync0 (slot_start=3000).
     * One step forward → alarm = 3000 + 3000 = 6000 (cell 2 of Sync0). */
    TdmaMachine_BootstrapFromSync(0u, 1u, 3000u);
    TEST_ASSERT_EQUAL(6000u, s_alarm_programmed);
    TEST_ASSERT_EQUAL(0u, TdmaMachine_GetCursor().phase_index);
    TEST_ASSERT_EQUAL(2u, TdmaMachine_GetCursor().cell_index);
}

/* =========================================================================
 * Key multi-Sync edge case: bootstrap at last cell of Sync0
 * ========================================================================= */

void test_2sync_bootstrap_last_cell_alarm_at_sync1_start(void)
{
    /* Receive at cell 2 (last cell) of Sync0, slot_start=6000.
     * Correct:  alarm = 6000 + 3000 = 9000  (Sync1, same frame).
     * Wrong A:  Frame_Epoch + frame_duration = 0 + 18000 = 18000.
     * Wrong B:  slot_start  + frame_duration = 6000 + 18000 = 24000. */
    TdmaMachine_BootstrapFromSync(0u, 2u, 6000u);
    TEST_ASSERT_EQUAL(SYNC1_START_MS, s_alarm_programmed);
}

void test_2sync_bootstrap_last_cell_cursor_at_sync1(void)
{
    /* Same bootstrap: cursor must be at Sync1 (phase 1, cell 0),
     * NOT at phase 0 cell 0 which would be the start of the next frame. */
    TdmaMachine_BootstrapFromSync(0u, 2u, 6000u);
    TEST_ASSERT_EQUAL(1u, TdmaMachine_GetCursor().phase_index);
    TEST_ASSERT_EQUAL(0u, TdmaMachine_GetCursor().cell_index);
}

/* =========================================================================
 * Integration — SlotTask runs correctly at Sync1 after bootstrap
 * ========================================================================= */

void test_2sync_slot_task_after_bootstrap_advances_within_sync1(void)
{
    /* Bootstrap at last cell of Sync0.  Alarm fires at 9000 (Sync1 cell 0).
     * One SlotTask call must advance cursor to Sync1 cell 1. */
    TdmaMachine_BootstrapFromSync(0u, 2u, 6000u);

    /* step_slot: sets s_rtc_ms = s_alarm_programmed (9000) and runs SlotTask */
    step_slot();

    TEST_ASSERT_EQUAL(1u, TdmaMachine_GetCursor().phase_index);
    TEST_ASSERT_EQUAL(1u, TdmaMachine_GetCursor().cell_index);
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_2sync_bootstrap_mid_cell_alarm);
    RUN_TEST(test_2sync_bootstrap_last_cell_alarm_at_sync1_start);
    RUN_TEST(test_2sync_bootstrap_last_cell_cursor_at_sync1);
    RUN_TEST(test_2sync_slot_task_after_bootstrap_advances_within_sync1);
    return UNITY_END();
}
