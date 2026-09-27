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
    TEST_ASSERT_EQUAL_UINT32(1000u, DayMsClock_Update(&c, 1000u, false));
    TEST_ASSERT_EQUAL_UINT32(1500u, DayMsClock_Update(&c, 1500u, false));
}

void test_clock_keeps_counting_across_midnight(void)
{
    /* The elapsed time a timer sees across midnight is the real one. */
    DayMsClock_t c;
    DayMsClock_Init(&c, MS_PER_DAY - 300u);
    uint32_t before = DayMsClock_Update(&c, MS_PER_DAY - 100u, false);
    uint32_t after  = DayMsClock_Update(&c, 200u, false);
    TEST_ASSERT_EQUAL_UINT32(300u, after - before);
    TEST_ASSERT_EQUAL_UINT32(MS_PER_DAY + 200u, after);
}

void test_clock_ignores_unknown_rtc_move_back(void)
{
    DayMsClock_t c;
    DayMsClock_Init(&c, 5000u);
    TEST_ASSERT_EQUAL_UINT32(5000u, DayMsClock_Update(&c, 4900u, false));  /* shifted back */
    /* 200 ms of real time after the shift: counted from the new reading. */
    TEST_ASSERT_EQUAL_UINT32(5200u, DayMsClock_Update(&c, 5100u, false));
}

void test_clock_differences_survive_the_2_32_wrap(void)
{
    DayMsClock_t c = { .mono_ms = 0xFFFFFF00u, .last_day_ms = 1000u, .pending_shift_ms = 0 };
    uint32_t t0 = c.mono_ms;
    uint32_t t1 = DayMsClock_Update(&c, 1500u, false);
    TEST_ASSERT_EQUAL_UINT32(500u, t1 - t0);
}

/* ------- RTC writes excluded: the counter follows real time ------------ */

void test_calendar_set_forward_is_not_counted(void)
{
    /* Packet 1 sets the RTC from 10:00:00.000 to 14:00:00.000. */
    DayMsClock_t c;
    DayMsClock_Init(&c, 36000000u);
    uint32_t t0 = DayMsClock_Update(&c, 36000000u, false);
    DayMsClock_OnCalendarSet(&c, 50400000u, false, 0);
    TEST_ASSERT_EQUAL_UINT32(t0 + 250u, DayMsClock_Update(&c, 50400250u, false));
}

void test_calendar_set_back_across_midnight_is_not_counted(void)
{
    /* Tier 3 at 00:00:00.100 sets the RTC back to 23:59:59.500. */
    DayMsClock_t c;
    DayMsClock_Init(&c, 100u);
    uint32_t t0 = c.mono_ms;
    DayMsClock_OnCalendarSet(&c, MS_PER_DAY - 500u, false, 0);
    TEST_ASSERT_EQUAL_UINT32(t0 + 800u, DayMsClock_Update(&c, 300u, false));
}

void test_shift_applied_at_once_is_not_counted(void)
{
    /* Tier 2 delays the clock by 40 ms; SHPF is already clear. */
    DayMsClock_t c;
    DayMsClock_Init(&c, 5000u);
    uint32_t t0 = DayMsClock_OnShift(&c, 5000u - 40u, false, -40);
    TEST_ASSERT_EQUAL_UINT32(5000u, t0);
    TEST_ASSERT_EQUAL_UINT32(5100u, DayMsClock_Update(&c, 5060u, false));
}

void test_shift_applied_later_is_not_counted(void)
{
    /* Tier 2 advances the clock by 30 ms; the hardware applies it later. */
    DayMsClock_t c;
    DayMsClock_Init(&c, 5000u);
    TEST_ASSERT_EQUAL_UINT32(5000u, DayMsClock_OnShift(&c, 5000u, true, 30));
    TEST_ASSERT_EQUAL_UINT32(5400u, DayMsClock_Update(&c, 5400u, true));  /* still pending */
    /* Applied between the two readings: 5400 + 300 real + 30 shift. */
    TEST_ASSERT_EQUAL_UINT32(5700u, DayMsClock_Update(&c, 5730u, false));
    TEST_ASSERT_EQUAL_UINT32(5800u, DayMsClock_Update(&c, 5830u, false));
}

void test_backward_shift_applied_later_is_not_counted(void)
{
    DayMsClock_t c;
    DayMsClock_Init(&c, 5000u);
    DayMsClock_OnShift(&c, 5000u, true, -60);
    /* Applied: 100 ms of real time, the reading moved back 60. */
    TEST_ASSERT_EQUAL_UINT32(5100u, DayMsClock_Update(&c, 5040u, false));
}

void test_calendar_set_with_its_shift_pending(void)
{
    /* Packet 1: set to 12:00:00 (whole second), then an advance shift of
     * 400 ms that the hardware applies later. */
    DayMsClock_t c;
    DayMsClock_Init(&c, 1000u);
    uint32_t t0 = c.mono_ms;
    DayMsClock_OnCalendarSet(&c, 43200000u, true, 400);
    TEST_ASSERT_EQUAL_UINT32(t0 + 100u, DayMsClock_Update(&c, 43200100u, true));
    TEST_ASSERT_EQUAL_UINT32(t0 + 300u, DayMsClock_Update(&c, 43200700u, false));
}

void test_calendar_set_with_its_shift_already_applied(void)
{
    DayMsClock_t c;
    DayMsClock_Init(&c, 1000u);
    uint32_t t0 = c.mono_ms;
    DayMsClock_OnCalendarSet(&c, 43200400u, false, 400);   /* shift in the reading */
    TEST_ASSERT_EQUAL_UINT32(t0 + 100u, DayMsClock_Update(&c, 43200500u, false));
}

void test_calendar_set_keeps_an_earlier_pending_shift(void)
{
    /* A Tier 2 shift is still pending when Tier 3 sets the calendar (no new
     * shift written); the hardware applies the old one after the set. */
    DayMsClock_t c;
    DayMsClock_Init(&c, 5000u);
    DayMsClock_OnShift(&c, 5000u, true, 50);
    uint32_t t0 = c.mono_ms;
    DayMsClock_OnCalendarSet(&c, 9000u, true, 0);
    TEST_ASSERT_EQUAL_UINT32(t0 + 100u, DayMsClock_Update(&c, 9150u, false));
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
    RUN_TEST(test_clock_ignores_unknown_rtc_move_back);
    RUN_TEST(test_clock_differences_survive_the_2_32_wrap);
    RUN_TEST(test_calendar_set_forward_is_not_counted);
    RUN_TEST(test_calendar_set_back_across_midnight_is_not_counted);
    RUN_TEST(test_shift_applied_at_once_is_not_counted);
    RUN_TEST(test_shift_applied_later_is_not_counted);
    RUN_TEST(test_backward_shift_applied_later_is_not_counted);
    RUN_TEST(test_calendar_set_with_its_shift_pending);
    RUN_TEST(test_calendar_set_with_its_shift_already_applied);
    RUN_TEST(test_calendar_set_keeps_an_earlier_pending_shift);
    return UNITY_END();
}
