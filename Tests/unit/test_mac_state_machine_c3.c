#include "unity.h"
#include "mac_state_machine.h"
#include "tdma_table.h"
#include <string.h>

/* ------- hook stubs ------------------------------------------------------- */

static int      s_sync_locked_calls;
static int      s_sync_lost_calls;
static uint32_t s_snapshot_ms;
static uint8_t  s_snapshot_day;
static uint8_t  s_snapshot_month;
static uint8_t  s_snapshot_year;

static void stub_sync_locked(void) { s_sync_locked_calls++; }
static void stub_sync_lost(void)   { s_sync_lost_calls++;   }

static void stub_get_rtc_snapshot(uint32_t *ms,
                                   uint8_t *day, uint8_t *month, uint8_t *year)
{
    *ms    = s_snapshot_ms;
    *day   = s_snapshot_day;
    *month = s_snapshot_month;
    *year  = s_snapshot_year;
}

static const MAC_Hooks_t k_hooks = {
    .rtc_set           = NULL,
    .get_rtc_snapshot  = stub_get_rtc_snapshot,
    .sync_bootstrapped = NULL,
    .sync_locked       = stub_sync_locked,
    .sync_lost         = stub_sync_lost,
};

/* ------- helpers ---------------------------------------------------------- */

static const Phase_t s_beacon_phase = {
    .type           = PHASE_TYPE_MESH_BEACON,
    .direction_mode = DIRECTION_MAC_CELL,
    .cell_count     = 10u,
    .slot_count     = 1u,
    .slot_active_ms = 2000u,
};

static const Phase_t s_other_phase = {
    .type           = PHASE_TYPE_MESH_UPLINK,
    .direction_mode = DIRECTION_CELL_SKIP,
    .cell_count     = 3u,
    .slot_active_ms = 2500u,
};

void setUp(void)
{
    s_sync_locked_calls = 0;
    s_sync_lost_calls   = 0;
    s_snapshot_ms       = 0u;
    s_snapshot_day      = 0x00u;
    s_snapshot_month    = 0x00u;
    s_snapshot_year     = 0x00u;
    MAC_Init(&k_hooks);
}

void tearDown(void) {}

/* ------- C3 boots Active -------------------------------------------------- */

void test_c3_boot_state_is_active(void)
{
    TEST_ASSERT_EQUAL(MAC_STATE_ACTIVE, MAC_GetState());
}

/* ------- Slot opportunity without Sync acquisition ----------------------- */

void test_c3_sync_phase_returns_tx_without_acquisition(void)
{
    FrameCursor_t c = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c, TdmaTable_GetPhase(0u)));
}

/* ------- phase_tx_flag always 1 for C3 ------------------------------------ */

void test_c3_phase_tx_flag_always_1_first_entry(void)
{
    FrameCursor_t c = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c, TdmaTable_GetPhase(0u));
    TEST_ASSERT_EQUAL(1u, MAC_GetPhaseTxFlag());
}

void test_c3_phase_tx_flag_stays_1_across_multiple_entries(void)
{
    FrameCursor_t c0 = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t co = {.phase_index = 1u, .cell_index = 0u, .slot_index = 0u};

    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));
    MAC_OnSlotOpportunity(&co, &s_other_phase);
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));
    MAC_OnSlotOpportunity(&co, &s_other_phase);
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));

    TEST_ASSERT_EQUAL(1u, MAC_GetPhaseTxFlag());
}

/* ------- C3 transmits cell 0, skips cells 1+ in Sync -------------------- */

void test_c3_sync_cell0_tx_cells1plus_skip(void)
{
    FrameCursor_t c0 = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t c1 = {.phase_index = 0u, .cell_index = 1u, .slot_index = 0u};
    FrameCursor_t c2 = {.phase_index = 0u, .cell_index = 2u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u)));
    TEST_ASSERT_EQUAL(SLOT_SKIP, MAC_OnSlotOpportunity(&c1, TdmaTable_GetPhase(0u)));
    TEST_ASSERT_EQUAL(SLOT_SKIP, MAC_OnSlotOpportunity(&c2, TdmaTable_GetPhase(0u)));
}

/* ------- Epoch captured at Sync phase entry ------------------------------- */

void test_c3_sync_phase_epoch_ms_captured_at_phase_entry(void)
{
    s_snapshot_ms = 43200000u;  /* 12:00:00.000 */
    FrameCursor_t c = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c, TdmaTable_GetPhase(0u));
    TEST_ASSERT_EQUAL(43200000u, MAC_GetSyncPhaseEpochMs());
}

void test_c3_sync_phase_date_captured_at_phase_entry(void)
{
    s_snapshot_ms    = 43200000u;
    s_snapshot_day   = 0x15u;
    s_snapshot_month = 0x06u;
    s_snapshot_year  = 0x25u;
    FrameCursor_t c = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c, TdmaTable_GetPhase(0u));

    uint8_t d, mo, y;
    MAC_GetSyncPhaseDate(&d, &mo, &y);
    TEST_ASSERT_EQUAL_HEX8(0x15u, d);
    TEST_ASSERT_EQUAL_HEX8(0x06u, mo);
    TEST_ASSERT_EQUAL_HEX8(0x25u, y);
}

void test_c3_sync_phase_date_not_captured_on_non_sync_phase(void)
{
    s_snapshot_ms  = 43200000u;
    s_snapshot_day = 0x15u;
    /* Beacon phase — should NOT call get_rtc_snapshot */
    FrameCursor_t c = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c, &s_beacon_phase);

    uint8_t d, mo, y;
    MAC_GetSyncPhaseDate(&d, &mo, &y);
    TEST_ASSERT_EQUAL_HEX8(0x00u, d);  /* not captured — stays at init value */
}

void test_c3_epoch_updated_on_each_sync_phase_entry(void)
{
    s_snapshot_ms = 10000000u;
    FrameCursor_t c0 = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t co = {.phase_index = 1u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));
    TEST_ASSERT_EQUAL(10000000u, MAC_GetSyncPhaseEpochMs());

    s_snapshot_ms = 20000000u;  /* next frame, different time */
    MAC_OnSlotOpportunity(&co, &s_other_phase);
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));
    TEST_ASSERT_EQUAL(20000000u, MAC_GetSyncPhaseEpochMs());
}

/* ------- BeaconTxBudget bypassed for C3 ----------------------------------- */

void test_c3_beacon_tx_budget_bypassed(void)
{
    FrameCursor_t c0 = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t c1 = {.phase_index = 5u, .cell_index = 1u, .slot_index = 0u};
    FrameCursor_t c2 = {.phase_index = 5u, .cell_index = 2u, .slot_index = 0u};
    FrameCursor_t c3 = {.phase_index = 5u, .cell_index = 3u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c0, &s_beacon_phase));
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c1, &s_beacon_phase));
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c2, &s_beacon_phase));
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c3, &s_beacon_phase));
}

/* ------- Epoch Received flag (always false for C3) ----------------------- */

void test_c3_epoch_received_always_false(void)
{
    TEST_ASSERT_FALSE(MAC_GetEpochReceivedThisPhase());
}

/* ------- main ------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_c3_boot_state_is_active);
    RUN_TEST(test_c3_sync_phase_returns_tx_without_acquisition);
    RUN_TEST(test_c3_phase_tx_flag_always_1_first_entry);
    RUN_TEST(test_c3_phase_tx_flag_stays_1_across_multiple_entries);
    RUN_TEST(test_c3_sync_cell0_tx_cells1plus_skip);
    RUN_TEST(test_c3_sync_phase_epoch_ms_captured_at_phase_entry);
    RUN_TEST(test_c3_sync_phase_date_captured_at_phase_entry);
    RUN_TEST(test_c3_sync_phase_date_not_captured_on_non_sync_phase);
    RUN_TEST(test_c3_epoch_updated_on_each_sync_phase_entry);
    RUN_TEST(test_c3_beacon_tx_budget_bypassed);

/* ------- Epoch Received flag (always false for C3) ----------------------- */

    RUN_TEST(test_c3_epoch_received_always_false);
    return UNITY_END();
}
