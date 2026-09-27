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

/* ------- DayMsClock: monotonic counter from time-of-day readings -------- */

void test_clock_starts_at_time_of_day(void)
{
    DayMsClock_t c;
    DayMsClock_Init(&c, 1000u);
    TEST_ASSERT_EQUAL_UINT32(1000u, DayMsClock_Update(&c, 1000u));
    TEST_ASSERT_EQUAL_UINT32(1500u, DayMsClock_Update(&c, 1500u));
}

void test_clock_keeps_counting_across_midnight(void)
{
    /* The elapsed time a timer sees across midnight is the real one. */
    DayMsClock_t c;
    DayMsClock_Init(&c, MS_PER_DAY - 300u);
    uint32_t before = DayMsClock_Update(&c, MS_PER_DAY - 100u);
    uint32_t after  = DayMsClock_Update(&c, 200u);
    TEST_ASSERT_EQUAL_UINT32(300u, after - before);
    TEST_ASSERT_EQUAL_UINT32(MS_PER_DAY + 200u, after);
}

void test_clock_ignores_rtc_moved_back(void)
{
    DayMsClock_t c;
    DayMsClock_Init(&c, 5000u);
    TEST_ASSERT_EQUAL_UINT32(5000u, DayMsClock_Update(&c, 4900u));  /* shifted back */
    /* 200 ms of real time after the shift: counted from the new reading. */
    TEST_ASSERT_EQUAL_UINT32(5200u, DayMsClock_Update(&c, 5100u));
}

void test_clock_differences_survive_the_2_32_wrap(void)
{
    DayMsClock_t c = { .mono_ms = 0xFFFFFF00u, .last_day_ms = 1000u };
    uint32_t t0 = c.mono_ms;
    uint32_t t1 = DayMsClock_Update(&c, 1500u);
    TEST_ASSERT_EQUAL_UINT32(500u, t1 - t0);
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
    RUN_TEST(test_clock_starts_at_time_of_day);
    RUN_TEST(test_clock_keeps_counting_across_midnight);
    RUN_TEST(test_clock_ignores_rtc_moved_back);
    RUN_TEST(test_clock_differences_survive_the_2_32_wrap);
    return UNITY_END();
}
