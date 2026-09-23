#include "unity.h"
#include "freq_resolver.h"
#include "shared_mem.h"
#include <string.h>

static FrequencyResolverState_t s_state;

void setUp(void)
{
    memset(&s_state, 0, sizeof(s_state));
    FrequencyResolver_Init(&s_state);
}

void tearDown(void) {}

/* --- STATIC mode ---------------------------------------------------------- */

void test_static_mode_returns_static_freq_for_cell_zero(void)
{
    s_state.phases[0].cell_mode          = CELL_FREQ_STATIC;
    s_state.phases[0].cell_freq_or_seed = 868100000u;
    FrequencyResolver_Init(&s_state);

    FrameCursor_t c = { .phase_index = 0u, .cell_index = 0u, .slot_index = 0u, .slot_pos = SLOT_POS_CELL };
    TEST_ASSERT_EQUAL(868100000u, FrequencyResolver_GetFreq(&c));
}

void test_static_mode_is_same_for_all_cell_indices(void)
{
    s_state.phases[0].cell_mode          = CELL_FREQ_STATIC;
    s_state.phases[0].cell_freq_or_seed = 868300000u;
    FrequencyResolver_Init(&s_state);

    FrameCursor_t c0 = { .phase_index = 0u, .cell_index = 0u,  .slot_index = 0u, .slot_pos = SLOT_POS_CELL };
    FrameCursor_t c7 = { .phase_index = 0u, .cell_index = 7u,  .slot_index = 0u, .slot_pos = SLOT_POS_CELL };
    FrameCursor_t c31 = { .phase_index = 0u, .cell_index = 31u, .slot_index = 0u, .slot_pos = SLOT_POS_CELL };
    TEST_ASSERT_EQUAL(868300000u, FrequencyResolver_GetFreq(&c0));
    TEST_ASSERT_EQUAL(868300000u, FrequencyResolver_GetFreq(&c7));
    TEST_ASSERT_EQUAL(868300000u, FrequencyResolver_GetFreq(&c31));
}

/* --- OVERRIDE mode -------------------------------------------------------- */

void test_override_mode_returns_correct_per_cell_freq(void)
{
    s_state.phases[0].cell_mode           = CELL_FREQ_OVERRIDE;
    s_state.phases[0].override_table_idx = 0u;
    g_freq_override_tables[0][0]          = 868100000u;
    g_freq_override_tables[0][1]          = 868300000u;
    g_freq_override_tables[0][2]          = 868500000u;
    FrequencyResolver_Init(&s_state);

    FrameCursor_t c0 = { .phase_index = 0u, .cell_index = 0u, .slot_index = 0u, .slot_pos = SLOT_POS_CELL };
    FrameCursor_t c1 = { .phase_index = 0u, .cell_index = 1u, .slot_index = 0u, .slot_pos = SLOT_POS_CELL };
    FrameCursor_t c2 = { .phase_index = 0u, .cell_index = 2u, .slot_index = 0u, .slot_pos = SLOT_POS_CELL };
    TEST_ASSERT_EQUAL(868100000u, FrequencyResolver_GetFreq(&c0));
    TEST_ASSERT_EQUAL(868300000u, FrequencyResolver_GetFreq(&c1));
    TEST_ASSERT_EQUAL(868500000u, FrequencyResolver_GetFreq(&c2));
}

void test_override_mode_different_cells_return_different_freqs(void)
{
    s_state.phases[0].cell_mode           = CELL_FREQ_OVERRIDE;
    s_state.phases[0].override_table_idx = 1u;
    g_freq_override_tables[1][0]          = 869525000u;
    g_freq_override_tables[1][1]          = 869725000u;
    FrequencyResolver_Init(&s_state);

    FrameCursor_t c0 = { .phase_index = 0u, .cell_index = 0u, .slot_index = 0u, .slot_pos = SLOT_POS_CELL };
    FrameCursor_t c1 = { .phase_index = 0u, .cell_index = 1u, .slot_index = 0u, .slot_pos = SLOT_POS_CELL };
    uint32_t f0 = FrequencyResolver_GetFreq(&c0);
    uint32_t f1 = FrequencyResolver_GetFreq(&c1);
    TEST_ASSERT_NOT_EQUAL(f0, f1);
}

/* --- Anchor positions ----------------------------------------------------- */

void test_header_pos_returns_header_freq(void)
{
    s_state.phases[0].header_freq_hz    = 867100000u;
    s_state.phases[0].cell_mode         = CELL_FREQ_STATIC;
    s_state.phases[0].cell_freq_or_seed = 868100000u;
    FrequencyResolver_Init(&s_state);

    FrameCursor_t c = { .phase_index = 0u, .cell_index = 0u, .slot_index = 0u, .slot_pos = SLOT_POS_HEADER };
    TEST_ASSERT_EQUAL(867100000u, FrequencyResolver_GetFreq(&c));
}

void test_footer_pos_returns_footer_freq(void)
{
    s_state.phases[0].footer_freq_hz    = 869100000u;
    s_state.phases[0].cell_mode         = CELL_FREQ_STATIC;
    s_state.phases[0].cell_freq_or_seed = 868100000u;
    FrequencyResolver_Init(&s_state);

    FrameCursor_t c = { .phase_index = 0u, .cell_index = 0u, .slot_index = 0u, .slot_pos = SLOT_POS_FOOTER };
    TEST_ASSERT_EQUAL(869100000u, FrequencyResolver_GetFreq(&c));
}

void test_header_footer_are_independent_of_cell_mode(void)
{
    s_state.phases[1].header_freq_hz    = 867500000u;
    s_state.phases[1].footer_freq_hz    = 869500000u;
    s_state.phases[1].cell_mode           = CELL_FREQ_OVERRIDE;
    s_state.phases[1].override_table_idx = 2u;
    g_freq_override_tables[2][0]          = 868500000u;
    FrequencyResolver_Init(&s_state);

    FrameCursor_t c = { .phase_index = 1u, .cell_index = 0u, .slot_index = 0u, .slot_pos = SLOT_POS_HEADER };
    TEST_ASSERT_EQUAL(867500000u, FrequencyResolver_GetFreq(&c));
    c.slot_pos = SLOT_POS_FOOTER;
    TEST_ASSERT_EQUAL(869500000u, FrequencyResolver_GetFreq(&c));
}

/* --- Absent slots --------------------------------------------------------- */

void test_absent_header_returns_zero(void)
{
    /* header_freq_hz left at 0 (memset in setUp) */
    FrameCursor_t c = { .phase_index = 0u, .cell_index = 0u, .slot_index = 0u, .slot_pos = SLOT_POS_HEADER };
    TEST_ASSERT_EQUAL(0u, FrequencyResolver_GetFreq(&c));
}

void test_absent_footer_returns_zero(void)
{
    FrameCursor_t c = { .phase_index = 0u, .cell_index = 0u, .slot_index = 0u, .slot_pos = SLOT_POS_FOOTER };
    TEST_ASSERT_EQUAL(0u, FrequencyResolver_GetFreq(&c));
}

void test_static_zero_returns_fallback(void)
{
    /* cell_freq_or_seed left at 0 — simulates uninitialised SRAM2 (CM4 not yet written) */
    s_state.phases[0].cell_mode = CELL_FREQ_STATIC;
    FrequencyResolver_Init(&s_state);

    FrameCursor_t c = { .phase_index = 0u, .cell_index = 0u, .slot_index = 0u, .slot_pos = SLOT_POS_CELL };
    TEST_ASSERT_EQUAL(FREQ_FALLBACK_HZ, FrequencyResolver_GetFreq(&c));
}

/* --- HOP mode (deferred) -------------------------------------------------- */

void test_hop_mode_returns_zero_while_deferred(void)
{
    s_state.phases[0].cell_mode        = CELL_FREQ_HOP;
    s_state.phases[0].cell_freq_or_seed = 0xDEADBEEFu;
    FrequencyResolver_Init(&s_state);

    FrameCursor_t c = { .phase_index = 0u, .cell_index = 0u, .slot_index = 0u, .slot_pos = SLOT_POS_CELL };
    TEST_ASSERT_EQUAL(0u, FrequencyResolver_GetFreq(&c));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_static_mode_returns_static_freq_for_cell_zero);
    RUN_TEST(test_static_mode_is_same_for_all_cell_indices);
    RUN_TEST(test_override_mode_returns_correct_per_cell_freq);
    RUN_TEST(test_override_mode_different_cells_return_different_freqs);
    RUN_TEST(test_header_pos_returns_header_freq);
    RUN_TEST(test_footer_pos_returns_footer_freq);
    RUN_TEST(test_header_footer_are_independent_of_cell_mode);
    RUN_TEST(test_absent_header_returns_zero);
    RUN_TEST(test_absent_footer_returns_zero);
    RUN_TEST(test_static_zero_returns_fallback);
    RUN_TEST(test_hop_mode_returns_zero_while_deferred);
    return UNITY_END();
}
