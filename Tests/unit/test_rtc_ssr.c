#include "unity.h"
#include <stdint.h>

/* Mirror of timer_if.c GetTimerTicks formula — pure math, no HAL dependency. */
#define RTC_PREDIV_S  4095u

static uint32_t ssr_to_ms(uint32_t ssr)
{
    return ((RTC_PREDIV_S - ssr) * 1000u) / (RTC_PREDIV_S + 1u);
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ssr_prediv_s_gives_zero_ms);
    RUN_TEST(test_ssr_2048_gives_approx_499ms);
    RUN_TEST(test_ssr_zero_gives_approx_999ms);
    RUN_TEST(test_shift_for_500ms_rounds_to_2048);
    RUN_TEST(test_shift_advance_subfs_for_100ms);
    RUN_TEST(test_shift_ticks_at_tier2_boundaries);
    return UNITY_END();
}
