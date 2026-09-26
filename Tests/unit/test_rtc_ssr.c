#include "unity.h"
#include <stdint.h>

/* Mirror of timer_if.c GetTimerTicks / SubSecondsToMs (CM0PLUS) — pure
 * math, no HAL dependency. */
#define RTC_PREDIV_S  4095u
#define MS_PER_DAY    86400000u

static uint32_t ssr_to_ms(uint32_t ssr)
{
    return ((RTC_PREDIV_S - ssr) * 1000u) / (RTC_PREDIV_S + 1u);
}

static int32_t sub_seconds_to_ms(uint32_t ssr)
{
    if (ssr <= RTC_PREDIV_S) {
        return (int32_t)ssr_to_ms(ssr);
    }
    return -(int32_t)(((ssr - RTC_PREDIV_S) * 1000u + RTC_PREDIV_S) / (RTC_PREDIV_S + 1u));
}

static uint32_t timer_ticks(uint32_t calendar_s, uint32_t ssr)
{
    int32_t ms = (int32_t)(calendar_s * 1000u) + sub_seconds_to_ms(ssr);
    if (ms < 0) {
        ms += (int32_t)MS_PER_DAY;
    }
    return (uint32_t)ms;
}

/* RTC model of mac_hook_rtc_set: SetTime(set_s) leaves SSR = PREDIV_S; the
 * SHIFTR advance (ADD1S=1, SUBFS) adds one calendar second and adds SUBFS
 * to the SSR down-counter (RM0453, RTC_SHIFTR). Returns the time read back. */
static uint32_t read_after_rtc_set(uint32_t set_s, uint32_t subsec_ms)
{
    uint32_t shift_ticks = (subsec_ms * (RTC_PREDIV_S + 1u)) / 1000u;
    uint32_t subfs       = (RTC_PREDIV_S + 1u) - shift_ticks;
    uint32_t calendar_s  = (set_s + 1u) % 86400u;
    return timer_ticks(calendar_s, RTC_PREDIV_S + subfs);
}

/* Inverse: given a target_ms, compute the SHIFTR SUBFS value needed to
 * bring SSR from PREDIV_S (reset value after HAL_RTC_SetTime) to the
 * SSR that represents target_ms into the current second. */
static uint32_t ms_to_shift(uint32_t target_ms)
{
    uint32_t target_ssr = RTC_PREDIV_S - (target_ms * (RTC_PREDIV_S + 1u)) / 1000u;
    return RTC_PREDIV_S - target_ssr; /* = (target_ms * (PREDIV_S+1)) / 1000 */
}

void setUp(void) {}
void tearDown(void) {}

/* ---- ssr_to_ms ---------------------------------------------------------- */


void test_ssr_prediv_s_gives_zero_ms(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, ssr_to_ms(RTC_PREDIV_S));
}

void test_ssr_2048_gives_approx_499ms(void)
{
    /* ((4095 - 2048) * 1000) / 4096 = 2047000 / 4096 = 499 ms */
    TEST_ASSERT_EQUAL_UINT32(499u, ssr_to_ms(2048u));
}

void test_ssr_zero_gives_approx_999ms(void)
{
    /* ((4095 - 0) * 1000) / 4096 = 4095000 / 4096 = 999 ms */
    TEST_ASSERT_EQUAL_UINT32(999u, ssr_to_ms(0u));
}

/* ---- ms_to_shift (inverse) --------------------------------------------- */

void test_shift_for_500ms_rounds_to_2048(void)
{
    /* 500 * 4096 / 1000 = 2048 */
    TEST_ASSERT_EQUAL_UINT32(2048u, ms_to_shift(500u));
}

/* ---- advance direction: SUBFS = (PREDIV_S+1) - shift_ticks ----------------- */

void test_shift_advance_subfs_for_100ms(void)
{
    /* delay  SUBFS for 100 ms: 100 * 4096 / 1000 = 409 */
    /* advance SUBFS for 100 ms: 4096 - 409 = 3687 */
    TEST_ASSERT_EQUAL_UINT32(409u,  ms_to_shift(100u));
    TEST_ASSERT_EQUAL_UINT32(3687u, (RTC_PREDIV_S + 1u) - ms_to_shift(100u));
}

void test_shift_ticks_at_tier2_boundaries(void)
{
    /* Lower bound 8 ms: 8 * 4096 / 1000 = 32 ticks */
    TEST_ASSERT_EQUAL_UINT32(32u, ms_to_shift(8u));
    /* Near upper bound 299 ms: 299 * 4096 / 1000 = 1224 ticks */
    TEST_ASSERT_EQUAL_UINT32(1224u, ms_to_shift(299u));
}

/* ---- SSR > PREDIV_S right after a SHIFTR advance ------------------------ */

void test_ssr_above_prediv_s_borrows_from_the_second(void)
{
    /* Calendar 00:00:04, SSR = PREDIV_S + 409 (~100 ms past PREDIV_S):
     * the clock actually reads 3.900 s. */
    TEST_ASSERT_EQUAL_UINT32(3900u, timer_ticks(4u, RTC_PREDIV_S + 409u));
}

void test_ssr_above_prediv_s_at_midnight_wraps_to_previous_day(void)
{
    TEST_ASSERT_EQUAL_UINT32(MS_PER_DAY - 100u, timer_ticks(0u, RTC_PREDIV_S + 409u));
}

void test_rtc_set_at_target_second_reads_target_ms(void)
{
    /* Bench case: target 3930 ms. Setting target_s then advancing by the
     * sub-second must read back 3930 ms (to one SSR tick). */
    uint32_t got = read_after_rtc_set(3u, 930u);
    TEST_ASSERT_UINT32_WITHIN(1u, 3930u, got);
}

void test_rtc_set_at_midnight_second_reads_target_ms(void)
{
    uint32_t got = read_after_rtc_set(0u, 933u);
    TEST_ASSERT_UINT32_WITHIN(1u, 933u, got);
}

void test_rtc_set_one_second_early_reads_a_second_short(void)
{
    /* The former hook set target_s - 1 before the same advance: the net
     * advance is only the sub-second, so the clock lagged by a full second
     * (observed on the C2 bench as ~1.1 s with the preamble latency). */
    uint32_t got = read_after_rtc_set(2u, 930u);
    TEST_ASSERT_UINT32_WITHIN(1u, 2930u, got);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ssr_prediv_s_gives_zero_ms);
    RUN_TEST(test_ssr_2048_gives_approx_499ms);
    RUN_TEST(test_ssr_zero_gives_approx_999ms);
    RUN_TEST(test_shift_for_500ms_rounds_to_2048);
    RUN_TEST(test_shift_advance_subfs_for_100ms);
    RUN_TEST(test_shift_ticks_at_tier2_boundaries);
    RUN_TEST(test_ssr_above_prediv_s_borrows_from_the_second);
    RUN_TEST(test_ssr_above_prediv_s_at_midnight_wraps_to_previous_day);
    RUN_TEST(test_rtc_set_at_target_second_reads_target_ms);
    RUN_TEST(test_rtc_set_at_midnight_second_reads_target_ms);
    RUN_TEST(test_rtc_set_one_second_early_reads_a_second_short);
    return UNITY_END();
}
