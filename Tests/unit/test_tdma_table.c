#include "unity.h"
#include "tdma_table.h"

void setUp(void)    { TdmaTable_Init(); }
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
    return UNITY_END();
}
