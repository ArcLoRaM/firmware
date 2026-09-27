#include "unity.h"
#include "day_ms.h"

/*
 * Midnight-safe arithmetic on ms-since-midnight RTC times (issue #54).
 */

void setUp(void)    {}
void tearDown(void) {}

void test_add_within_day(void)
{
    TEST_ASSERT_EQUAL_UINT32(3000u, DayMs_Add(1000u, 2000));
    TEST_ASSERT_EQUAL_UINT32(800u, DayMs_Add(1000u, -200));
}

void test_add_wraps_forward_past_midnight(void)
{
    TEST_ASSERT_EQUAL_UINT32(100u, DayMs_Add(MS_PER_DAY - 2900u, 3000));
    TEST_ASSERT_EQUAL_UINT32(0u, DayMs_Add(MS_PER_DAY - 1u, 1));
}

void test_add_wraps_back_before_midnight(void)
{
    TEST_ASSERT_EQUAL_UINT32(MS_PER_DAY - 100u, DayMs_Add(100u, -200));
    TEST_ASSERT_EQUAL_UINT32(MS_PER_DAY - 1u, DayMs_Add(0u, -1));
}

void test_add_reduces_an_out_of_domain_input(void)
{
    TEST_ASSERT_EQUAL_UINT32(1000u, DayMs_Add(MS_PER_DAY + 1000u, 0));
}

void test_diff_sign_within_day(void)
{
    TEST_ASSERT_EQUAL_INT32(2000, DayMs_Diff(3000u, 1000u));
    TEST_ASSERT_EQUAL_INT32(-2000, DayMs_Diff(1000u, 3000u));
    TEST_ASSERT_EQUAL_INT32(0, DayMs_Diff(5u, 5u));
}

void test_diff_across_midnight_takes_the_short_way(void)
{
    /* 00:00:00.100 is 200 ms after 23:59:59.900, not ~24 h before it. */
    TEST_ASSERT_EQUAL_INT32(200, DayMs_Diff(100u, MS_PER_DAY - 100u));
    TEST_ASSERT_EQUAL_INT32(-200, DayMs_Diff(MS_PER_DAY - 100u, 100u));
}

void test_diff_boundary_is_plus_twelve_hours(void)
{
    TEST_ASSERT_EQUAL_INT32((int32_t)(MS_PER_DAY / 2u), DayMs_Diff(MS_PER_DAY / 2u, 0u));
    TEST_ASSERT_EQUAL_INT32(-(int32_t)(MS_PER_DAY / 2u) + 1,
                            DayMs_Diff(MS_PER_DAY / 2u + 1u, 0u));
}

void test_abs_diff_across_midnight(void)
{
    TEST_ASSERT_EQUAL_UINT32(200u, DayMs_AbsDiff(MS_PER_DAY - 100u, 100u));
    TEST_ASSERT_EQUAL_UINT32(200u, DayMs_AbsDiff(100u, MS_PER_DAY - 100u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_add_within_day);
    RUN_TEST(test_add_wraps_forward_past_midnight);
    RUN_TEST(test_add_wraps_back_before_midnight);
    RUN_TEST(test_add_reduces_an_out_of_domain_input);
    RUN_TEST(test_diff_sign_within_day);
    RUN_TEST(test_diff_across_midnight_takes_the_short_way);
    RUN_TEST(test_diff_boundary_is_plus_twelve_hours);
    RUN_TEST(test_abs_diff_across_midnight);
    return UNITY_END();
}
