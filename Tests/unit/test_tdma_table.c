#include "unity.h"
#include "tdma_table.h"
#include <stdio.h>

void setUp(void)    { TdmaTable_Init(); }
void tearDown(void) {}

/* --- PhaseCount --------------------------------------------------------- */

void test_table_has_two_phases(void)
{
    TEST_ASSERT_EQUAL(2u, TdmaTable_PhaseCount());
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

void test_get_phase_one_type_is_sync(void)
{
    const Phase_t *p = TdmaTable_GetPhase(1u);
    TEST_ASSERT_EQUAL(PHASE_TYPE_SYNC, p->type);
}

void test_get_phase_one_participant_mask_is_c1_and_c2_and_c3(void)
{
    const Phase_t *p = TdmaTable_GetPhase(1u);
    TEST_ASSERT_EQUAL_HEX8(PARTICIPANT_C1 | PARTICIPANT_C2 | PARTICIPANT_C3, p->participant_mask);
}

void test_get_phase_one_direction_mode_is_mac_cell(void)
{
    const Phase_t *p = TdmaTable_GetPhase(1u);
    TEST_ASSERT_EQUAL(DIRECTION_MAC_CELL, p->direction_mode);
}

void test_get_phase_out_of_bounds_returns_null(void)
{
    TEST_ASSERT_NULL(TdmaTable_GetPhase(2u));
    TEST_ASSERT_NULL(TdmaTable_GetPhase(255u));
}

/* --- PhaseStartOffset --------------------------------------------------- */

void test_phase_start_offset_zero_is_zero(void)
{
    TEST_ASSERT_EQUAL(0u, TdmaTable_PhaseStartOffset_ms(0u));
}

void test_phase_start_offset_one_is_9000(void)
{
    /* per_cell = 1 * (2500 + 500) = 3000; 3 cells → 9000 ms */
    TEST_ASSERT_EQUAL(9000u, TdmaTable_PhaseStartOffset_ms(1u));
}

void test_phase_start_offset_out_of_bounds_returns_zero(void)
{
    TEST_ASSERT_EQUAL(0u, TdmaTable_PhaseStartOffset_ms(2u));
}

/* --- Sync cell spacing constraint ---------------------------------------- */

/* HAL_RTCEx_SetSynchroShift busy-polls SHPF until the previous shift is
 * absorbed at the next RTC second boundary (1 Hz). If two sync receptions
 * triggering Tier 2 correction fall within the same RTC second, the second
 * call blocks the UTIL_SEQ task loop for up to 1 s, causing the node to
 * miss its next TDMA slot.
 *
 * To prevent this, every PHASE_TYPE_SYNC phase must have:
 *   per_cell = slot_count * (slot_active_ms + gap_after_slot_ms) >= 1000 ms
 *
 * See ADR-0009 "Sync cell spacing constraint for HAL_RTCEx_SetSynchroShift".
 */
void test_sync_phase_per_cell_ge_1000ms(void)
{
    uint8_t count = TdmaTable_PhaseCount();
    for (uint8_t i = 0u; i < count; i++) {
        const Phase_t *p = TdmaTable_GetPhase(i);
        TEST_ASSERT_NOT_NULL(p);
        if (p->type != PHASE_TYPE_SYNC) continue;

        uint32_t per_cell = (uint32_t)p->slot_count
                          * (p->slot_active_ms + p->gap_after_slot_ms);
        char msg[64];
        snprintf(msg, sizeof(msg), "phase %u: per_cell=%u < 1000", i, per_cell);
        TEST_ASSERT_MESSAGE(per_cell >= 1000u, msg);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_table_has_two_phases);
    RUN_TEST(test_get_phase_zero_is_not_null);
    RUN_TEST(test_get_phase_zero_type_is_sync);
    RUN_TEST(test_get_phase_zero_participant_mask_is_c1_and_c2_and_c3);
    RUN_TEST(test_get_phase_zero_direction_mode_is_mac_cell);
    RUN_TEST(test_get_phase_one_type_is_sync);
    RUN_TEST(test_get_phase_one_participant_mask_is_c1_and_c2_and_c3);
    RUN_TEST(test_get_phase_one_direction_mode_is_mac_cell);
    RUN_TEST(test_get_phase_out_of_bounds_returns_null);
    RUN_TEST(test_phase_start_offset_zero_is_zero);
    RUN_TEST(test_phase_start_offset_one_is_9000);
    RUN_TEST(test_phase_start_offset_out_of_bounds_returns_zero);
    RUN_TEST(test_sync_phase_per_cell_ge_1000ms);
    return UNITY_END();
}
