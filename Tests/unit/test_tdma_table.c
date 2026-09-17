#include "unity.h"
#include "tdma_table.h"

void setUp(void)    {}
void tearDown(void) {}

/* --- PhaseCount --------------------------------------------------------- */

void test_stub_table_has_one_phase(void)
{
    TEST_ASSERT_EQUAL(1u, TdmaTable_PhaseCount());
}

/* --- GetPhase ------------------------------------------------------------ */

void test_get_phase_zero_is_not_null(void)
{
    TEST_ASSERT_NOT_NULL(TdmaTable_GetPhase(0u));
}

void test_get_phase_zero_type_is_sync(void)
{
    const Phase_t *p = TdmaTable_GetPhase(0u);
    TEST_ASSERT_EQUAL(PHASE_TYPE_SYNC, p->type);
}

void test_get_phase_zero_participant_mask_is_c1_and_c2_and_c3(void)
{
    /* bit1=C1, bit2=C2, bit3=C3 → 0x07 */
    const Phase_t *p = TdmaTable_GetPhase(0u);
    TEST_ASSERT_EQUAL_HEX8(PARTICIPANT_C1 | PARTICIPANT_C2 | PARTICIPANT_C3, p->participant_mask);
}

void test_get_phase_zero_direction_mode_is_mac_cell(void)
{
    const Phase_t *p = TdmaTable_GetPhase(0u);
    TEST_ASSERT_EQUAL(DIRECTION_MAC_CELL, p->direction_mode);
}

void test_get_phase_out_of_bounds_returns_null(void)
{
    TEST_ASSERT_NULL(TdmaTable_GetPhase(1u));
    TEST_ASSERT_NULL(TdmaTable_GetPhase(255u));
}

/* --- PhaseStartOffset --------------------------------------------------- */

void test_phase_start_offset_zero_is_zero(void)
{
    TEST_ASSERT_EQUAL(0u, TdmaTable_PhaseStartOffset_ms(0u));
}

void test_phase_start_offset_out_of_bounds_returns_zero(void)
{
    TEST_ASSERT_EQUAL(0u, TdmaTable_PhaseStartOffset_ms(1u));
}

/* --- ValidateSyncSingleSlot ------------------------------------------------
 *
 * TdmaMachine_BootstrapFromSync always re-anchors to slot 0 of the received
 * cell, because SyncPayload_t carries no field identifying which slot within
 * a cell it was received in. That's only correct if every Sync phase has
 * exactly one slot per cell. See CM0PLUS/SubGHz_Phy/Logic/tdma_machine.c.
 */

void test_validate_sync_single_slot_passes_on_real_table(void)
{
    TEST_ASSERT_TRUE(TdmaTable_ValidateSyncSingleSlot());
}

void test_validate_sync_single_slot_rejects_multi_slot_sync_phase(void)
{
    static const Phase_t bad_sync_phase = {
        .type       = PHASE_TYPE_SYNC,
        .cell_count = 3u,
        .slot_count = 2u,  /* violates the one-slot-per-cell Sync invariant */
    };
    static const Phase_t * const bad_table[] = { &bad_sync_phase };

    TEST_ASSERT_FALSE(TdmaTable_ValidateSyncSingleSlot_Of(bad_table, 1u));
}

void test_validate_sync_single_slot_ignores_non_sync_multi_slot_phase(void)
{
    static const Phase_t beacon_phase = {
        .type       = PHASE_TYPE_MESH_BEACON,
        .cell_count = 10u,
        .slot_count = 3u,  /* fine — invariant only applies to PHASE_TYPE_SYNC */
    };
    static const Phase_t * const table[] = { &beacon_phase };

    TEST_ASSERT_TRUE(TdmaTable_ValidateSyncSingleSlot_Of(table, 1u));
}

void test_validate_sync_single_slot_accepts_single_slot_sync_phase(void)
{
    static const Phase_t good_sync_phase = {
        .type       = PHASE_TYPE_SYNC,
        .cell_count = 3u,
        .slot_count = 1u,
    };
    static const Phase_t * const table[] = { &good_sync_phase };

    TEST_ASSERT_TRUE(TdmaTable_ValidateSyncSingleSlot_Of(table, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stub_table_has_one_phase);
    RUN_TEST(test_get_phase_zero_is_not_null);
    RUN_TEST(test_get_phase_zero_type_is_sync);
    RUN_TEST(test_get_phase_zero_participant_mask_is_c1_and_c2_and_c3);
    RUN_TEST(test_get_phase_zero_direction_mode_is_mac_cell);
    RUN_TEST(test_get_phase_out_of_bounds_returns_null);
    RUN_TEST(test_phase_start_offset_zero_is_zero);
    RUN_TEST(test_phase_start_offset_out_of_bounds_returns_zero);
    RUN_TEST(test_validate_sync_single_slot_passes_on_real_table);
    RUN_TEST(test_validate_sync_single_slot_rejects_multi_slot_sync_phase);
    RUN_TEST(test_validate_sync_single_slot_ignores_non_sync_multi_slot_phase);
    RUN_TEST(test_validate_sync_single_slot_accepts_single_slot_sync_phase);
    return UNITY_END();
}
