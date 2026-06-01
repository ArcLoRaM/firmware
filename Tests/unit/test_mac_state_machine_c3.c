#include "unity.h"
#include "mac_state_machine.h"
#include "tdma_table.h"
#include <string.h>

/* ------- hook stubs ------------------------------------------------------- */

static int s_sync_locked_calls;
static int s_sync_lost_calls;

static void stub_sync_locked(void) { s_sync_locked_calls++; }
static void stub_sync_lost(void)   { s_sync_lost_calls++;   }

static const MAC_Hooks_t k_hooks = {
    .rtc_set             = NULL,
    .rtc_align_subsecond = NULL,
    .sync_locked         = stub_sync_locked,
    .sync_lost           = stub_sync_lost,
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
    /* C3 is the SyncAnchor — it immediately uses the Sync phase for TX */
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
    FrameCursor_t c0  = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t co  = {.phase_index = 1u, .cell_index = 0u, .slot_index = 0u};

    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));   /* entry 1 */
    MAC_OnSlotOpportunity(&co, &s_other_phase);            /* force phase change */
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));   /* entry 2 */
    MAC_OnSlotOpportunity(&co, &s_other_phase);
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));   /* entry 3 */

    TEST_ASSERT_EQUAL(1u, MAC_GetPhaseTxFlag());
}

/* ------- BeaconTxBudget bypassed for C3 ----------------------------------- */

void test_c3_beacon_tx_budget_bypassed(void)
{
    /*
     * C3 has no beacon received to set budget.  Without the bypass, cells
     * 0 and 1 would return RX (budget=0).  With the bypass they return TX.
     */
    FrameCursor_t c0 = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t c1 = {.phase_index = 5u, .cell_index = 1u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c0, &s_beacon_phase));
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c1, &s_beacon_phase));
}

/* ------- main ------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_c3_boot_state_is_active);
    RUN_TEST(test_c3_sync_phase_returns_tx_without_acquisition);
    RUN_TEST(test_c3_phase_tx_flag_always_1_first_entry);
    RUN_TEST(test_c3_phase_tx_flag_stays_1_across_multiple_entries);
    RUN_TEST(test_c3_beacon_tx_budget_bypassed);
    return UNITY_END();
}
