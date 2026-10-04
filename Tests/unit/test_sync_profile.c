#include "unity.h"
#include <string.h>
#include "sync_profile.h"

/*
 * The source default of the Sync profile (no SYNC_PROFILE given): DEV, the
 * frequent schedule inside the duty-cycle budget. Built without the define.
 */

void setUp(void) {}
void tearDown(void) {}

void test_the_default_profile_is_dev(void)
{
    TEST_ASSERT_EQUAL_INT(SYNC_PROFILE_DEV, SYNC_PROFILE);
    TEST_ASSERT_EQUAL_STRING("DEV", SYNC_PROFILE_NAME);
}

void test_dev_is_one_packet_per_200_s_phase(void)
{
    TEST_ASSERT_EQUAL_UINT32(1u, SYNC_TX_BUDGET);
    /* ten cells of a 2500 ms slot and the gap: a 200 s phase */
    TEST_ASSERT_EQUAL_UINT32(200000u, 10u * (2500u + SYNC_CELL_GAP_MS));
}

/* One packet of 991 ms per 200 s phase is under half of the 1 % band. */
void test_dev_duty_cycle_is_under_half_a_percent(void)
{
    uint32_t phase_ms = 10u * (2500u + SYNC_CELL_GAP_MS);
    TEST_ASSERT_TRUE(991u * 1000u * SYNC_TX_BUDGET < phase_ms * 5u);
}

void test_an_explicit_override_wins_over_the_profile(void)
{
    /* the constants are #ifndef: this build has none, so they are the profile's */
    TEST_ASSERT_EQUAL_UINT32(SYNC_PROFILE_TX_BUDGET, SYNC_TX_BUDGET);
    TEST_ASSERT_EQUAL_UINT32(SYNC_PROFILE_CELL_GAP_MS, SYNC_CELL_GAP_MS);
    TEST_ASSERT_EQUAL_UINT32(SYNC_PROFILE_SILENCE_MS, SYNC_SILENCE_TIMEOUT_MS);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_default_profile_is_dev);
    RUN_TEST(test_dev_is_one_packet_per_200_s_phase);
    RUN_TEST(test_dev_duty_cycle_is_under_half_a_percent);
    RUN_TEST(test_an_explicit_override_wins_over_the_profile);
    return UNITY_END();
}
