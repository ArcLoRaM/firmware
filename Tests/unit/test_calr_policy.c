#include "unity.h"
#include "calr_policy.h"

/*
 * When a new RTC_CALR is written (issue #34): the residual must reach 3/4 of
 * a step, the estimate must be valid, and the setting must change.
 */

void setUp(void) {}
void tearDown(void) {}

/* An estimate as the estimator would report it with `current` applied. */
static DriftEstimate_t est(int32_t rate_ppb, RtcCalr_t current, bool valid)
{
    DriftEstimate_t e = { valid, rate_ppb, rate_ppb + RtcCalr_ToPpb(current), 360u, 40u, 1800u };
    return e;
}

static const RtcCalr_t NONE = { 0, 0 };

void test_threshold_is_three_quarters_of_a_step(void)
{
    TEST_ASSERT_EQUAL_INT32(715, CALR_WRITE_THRESHOLD_PPB);
}

void test_an_invalid_estimate_never_writes(void)
{
    DriftEstimate_t e = est(8100, NONE, false);
    TEST_ASSERT_FALSE(CalrPolicy_Decide(&e, NONE).write);
}

/* First convergence: jump to the setting nearest to cancelling the rate. */
void test_the_first_write_cancels_the_whole_rate(void)
{
    DriftEstimate_t e = est(8100, NONE, true);
    CalrDecision_t d = CalrPolicy_Decide(&e, NONE);
    TEST_ASSERT_TRUE(d.write);
    TEST_ASSERT_EQUAL_INT32(-8100, d.req_ppb);
    TEST_ASSERT_EQUAL_UINT8(0, d.calr.calp);
    TEST_ASSERT_EQUAL_UINT16(8, d.calr.calm);          /* -7629 ppb, residual +471 */
}

void test_a_slow_clock_writes_calp(void)
{
    DriftEstimate_t e = est(-5000, NONE, true);
    CalrDecision_t d = CalrPolicy_Decide(&e, NONE);
    TEST_ASSERT_TRUE(d.write);
    TEST_ASSERT_EQUAL_UINT8(1, d.calr.calp);
    TEST_ASSERT_EQUAL_UINT16(507, d.calr.calm);        /* N = +5 */
}

/* Settled: the residual (471) is under the threshold. */
void test_no_write_while_the_residual_is_small(void)
{
    RtcCalr_t cur = RtcCalr_FromPulses(-8);
    DriftEstimate_t e = est(8100, cur, true);
    TEST_ASSERT_EQUAL_INT32(471, e.residual_ppb);
    TEST_ASSERT_FALSE(CalrPolicy_Decide(&e, cur).write);
}

/* Drift: 8.3 ppm leaves 671 (no write), 8.4 ppm leaves 771: one step. */
void test_a_drifting_rate_writes_one_step_at_three_quarters(void)
{
    RtcCalr_t cur = RtcCalr_FromPulses(-8);
    DriftEstimate_t e1 = est(8300, cur, true);
    TEST_ASSERT_FALSE(CalrPolicy_Decide(&e1, cur).write);
    DriftEstimate_t e2 = est(8400, cur, true);
    CalrDecision_t d = CalrPolicy_Decide(&e2, cur);
    TEST_ASSERT_TRUE(d.write);
    TEST_ASSERT_EQUAL_INT32(-9, RtcCalr_Pulses(d.calr));
}

void test_the_threshold_is_inclusive(void)
{
    RtcCalr_t cur = RtcCalr_FromPulses(-8);                    /* applied -7629 */
    DriftEstimate_t e = est(7629 + CALR_WRITE_THRESHOLD_PPB, cur, true);
    TEST_ASSERT_EQUAL_INT32(CALR_WRITE_THRESHOLD_PPB, e.residual_ppb);
    TEST_ASSERT_TRUE(CalrPolicy_Decide(&e, cur).write);
}

/* The rate sits on the middle of two settings: noise of +-200 ppb on the
 * estimate must not make the setting flip back and forth. */
void test_noise_around_the_middle_of_two_steps_does_not_dither(void)
{
    for (int start = -8; start >= -9; start--) {
        RtcCalr_t cur = RtcCalr_FromPulses(start);
        for (int32_t rate = 7900; rate <= 8300; rate += 10) {
            DriftEstimate_t e = est(rate, cur, true);
            TEST_ASSERT_FALSE_MESSAGE(CalrPolicy_Decide(&e, cur).write, "dithered");
        }
    }
}

/* At the end of the range there is nothing further to write. */
void test_no_write_when_saturated(void)
{
    RtcCalr_t top = RtcCalr_FromPulses(RTC_CALR_N_MAX);
    DriftEstimate_t e = est(-600000, top, true);
    TEST_ASSERT_TRUE(e.residual_ppb < -CALR_WRITE_THRESHOLD_PPB);
    TEST_ASSERT_FALSE(CalrPolicy_Decide(&e, top).write);
}

/* Cancelling a rate that fell back to zero: back to no calibration. */
void test_a_rate_that_goes_away_writes_zero(void)
{
    RtcCalr_t cur = RtcCalr_FromPulses(-8);
    DriftEstimate_t e = est(0, cur, true);
    CalrDecision_t d = CalrPolicy_Decide(&e, cur);
    TEST_ASSERT_TRUE(d.write);
    TEST_ASSERT_EQUAL_INT32(0, RtcCalr_Pulses(d.calr));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_threshold_is_three_quarters_of_a_step);
    RUN_TEST(test_an_invalid_estimate_never_writes);
    RUN_TEST(test_the_first_write_cancels_the_whole_rate);
    RUN_TEST(test_a_slow_clock_writes_calp);
    RUN_TEST(test_no_write_while_the_residual_is_small);
    RUN_TEST(test_a_drifting_rate_writes_one_step_at_three_quarters);
    RUN_TEST(test_the_threshold_is_inclusive);
    RUN_TEST(test_noise_around_the_middle_of_two_steps_does_not_dither);
    RUN_TEST(test_no_write_when_saturated);
    RUN_TEST(test_a_rate_that_goes_away_writes_zero);
    return UNITY_END();
}
