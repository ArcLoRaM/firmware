#include "unity.h"
#include "mac_state_machine.h"
#include "tdma_table.h"
#include "arclog_capture.h"
#include <string.h>

/*
 * Boot burst (issue #45, ADR-0020): the C3 sends SYNC_BOOT_BURST packets, in
 * the first cells, of the FIRST Sync phase after it boots, so that boards
 * flashed or reset together with it lock within that phase; every later
 * phase sends SYNC_TX_BUDGET (1). Built with SYNC_BOOT_BURST=5u, the DEV
 * profile (one packet per phase), by CMake.
 */

static const Phase_t s_sync_phase = {
    .type              = PHASE_TYPE_SYNC,
    .direction_mode    = DIRECTION_MAC_CELL,
    .cell_count        = 10u,
    .slot_count        = 1u,
    .slot_active_ms    = 2500u,
    .gap_after_slot_ms = 17500u,
};

static const Phase_t s_other_phase = {
    .type           = PHASE_TYPE_MESH_UPLINK,
    .direction_mode = DIRECTION_CELL_SKIP,
    .cell_count     = 3u,
    .slot_active_ms = 2500u,
};

void setUp(void)
{
    MAC_Init(NULL);
    ArcLog_CaptureReset();
}
void tearDown(void) {}

static SlotDecision_t cell(uint8_t phase_idx, const Phase_t *ph, uint16_t c)
{
    FrameCursor_t cur = { .phase_index = phase_idx, .cell_index = c, .slot_index = 0u };
    return MAC_OnSlotOpportunity(&cur, ph, 0u);
}

static int tx_cells_of_a_phase(uint8_t phase_idx)
{
    int tx = 0;
    for (uint16_t c = 0u; c < s_sync_phase.cell_count; c++) {
        if (cell(phase_idx, &s_sync_phase, c) == SLOT_TX) tx++;
    }
    return tx;
}

void test_the_first_sync_phase_after_boot_sends_the_burst(void)
{
    for (uint16_t c = 0u; c < 5u; c++) {
        TEST_ASSERT_EQUAL_MESSAGE(SLOT_TX, cell(0u, &s_sync_phase, c), "burst cell not a Tx");
    }
    for (uint16_t c = 5u; c < 10u; c++) {
        TEST_ASSERT_EQUAL_MESSAGE(SLOT_SKIP, cell(0u, &s_sync_phase, c), "cell after the burst not skipped");
    }
}

void test_every_later_sync_phase_sends_one_packet(void)
{
    TEST_ASSERT_EQUAL_INT(5, tx_cells_of_a_phase(0u));     /* the burst */
    TEST_ASSERT_EQUAL_INT(1, tx_cells_of_a_phase(1u));     /* the second Sync phase of the frame */
    TEST_ASSERT_EQUAL_INT(1, tx_cells_of_a_phase(0u));     /* the next frame */
}

/* The burst is for the first Sync phase, not for the first phase of any kind. */
void test_a_phase_that_is_not_sync_does_not_use_up_the_burst(void)
{
    (void)cell(2u, &s_other_phase, 0u);
    TEST_ASSERT_EQUAL_INT(5, tx_cells_of_a_phase(0u));
}

/* A reboot is a new burst. */
void test_a_reboot_sends_the_burst_again(void)
{
    TEST_ASSERT_EQUAL_INT(5, tx_cells_of_a_phase(0u));
    TEST_ASSERT_EQUAL_INT(1, tx_cells_of_a_phase(1u));
    MAC_Init(NULL);
    TEST_ASSERT_EQUAL_INT(5, tx_cells_of_a_phase(0u));
}

void test_the_budget_getter_follows_the_burst(void)
{
    TEST_ASSERT_EQUAL_UINT32(SYNC_TX_BUDGET, MAC_GetSyncTxBudget());   /* after init: the steady budget */
    (void)cell(0u, &s_sync_phase, 0u);
    TEST_ASSERT_EQUAL_UINT32(4u, MAC_GetSyncTxBudget());                /* 5 - 1 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_first_sync_phase_after_boot_sends_the_burst);
    RUN_TEST(test_every_later_sync_phase_sends_one_packet);
    RUN_TEST(test_a_phase_that_is_not_sync_does_not_use_up_the_burst);
    RUN_TEST(test_a_reboot_sends_the_burst_again);
    RUN_TEST(test_the_budget_getter_follows_the_burst);
    return UNITY_END();
}
