#include "unity.h"
#include <stdint.h>
#include "rtc_calr.h"

/*
 * ppb <-> RTC_CALR (CALP, CALM, 32 s window), issue #34.
 * Reference: RM0453 smooth digital calibration,
 *   F_cal = F_rtcclk x (1 + (CALP x 512 - CALM) / (2^20 + CALM - CALP x 512)).
 */

void setUp(void) {}
void tearDown(void) {}

static void assert_setting(uint8_t calp, uint16_t calm, RtcCalr_t c)
{
    TEST_ASSERT_EQUAL_UINT8(calp, c.calp);
    TEST_ASSERT_EQUAL_UINT16(calm, c.calm);
}

void test_zero_is_no_calibration(void)
{
    assert_setting(0, 0, RtcCalr_FromPpb(0));
    TEST_ASSERT_EQUAL_INT32(0, RtcCalr_ToPpb(RtcCalr_FromPpb(0)));
}

/* A fast RTC (+8 ppm against its sender) needs a negative correction: CALM
 * alone masks pulses. 8000 ppb = 8.39 steps. */
void test_fast_rtc_is_slowed_with_calm_alone(void)
{
    RtcCalr_t c = RtcCalr_FromPpb(-8000);
    assert_setting(0, 8, c);
    TEST_ASSERT_EQUAL_INT32(-7629, RtcCalr_ToPpb(c));   /* -8e9 / (2^20 + 8) */
}

/* A slow RTC uses CALP = 1 with CALM = 512 - N. */
void test_slow_rtc_uses_calp_with_calm_512_minus_n(void)
{
    RtcCalr_t c = RtcCalr_FromPpb(8000);
    assert_setting(1, 504, c);
    TEST_ASSERT_EQUAL_INT32(7629, RtcCalr_ToPpb(c));    /* +8e9 / (2^20 - 8) */
}

void test_one_step_either_side_of_zero(void)
{
    RtcCalr_t up = RtcCalr_FromPulses(1);
    assert_setting(1, 511, up);
    TEST_ASSERT_EQUAL_INT32(954, RtcCalr_ToPpb(up));
    RtcCalr_t dn = RtcCalr_FromPulses(-1);
    assert_setting(0, 1, dn);
    TEST_ASSERT_EQUAL_INT32(-954, RtcCalr_ToPpb(dn));
}

/* The range of the issue: -487.1 ppm (CALM = 511) to +488.5 ppm (CALP, CALM = 0). */
void test_range_edges(void)
{
    RtcCalr_t hi = RtcCalr_FromPpb(488520);
    assert_setting(1, 0, hi);
    TEST_ASSERT_EQUAL_INT32(488520, RtcCalr_ToPpb(hi));      /* 488 519.79 */

    RtcCalr_t lo = RtcCalr_FromPpb(-487090);
    assert_setting(0, 511, lo);
    TEST_ASSERT_EQUAL_INT32(-487090, RtcCalr_ToPpb(lo));     /* -487 090.20 */
}

void test_beyond_the_range_saturates(void)
{
    assert_setting(1, 0, RtcCalr_FromPpb(600000));
    assert_setting(1, 0, RtcCalr_FromPpb(INT32_MAX));
    assert_setting(0, 511, RtcCalr_FromPpb(-600000));
    assert_setting(0, 511, RtcCalr_FromPpb(INT32_MIN));
    /* One ppb inside the edge is not saturated: still the edge setting. */
    assert_setting(1, 0, RtcCalr_FromPpb(488519));
    assert_setting(1, 1, RtcCalr_FromPpb(487565));           /* N = 511 */
}

void test_rounds_to_the_nearest_step_symmetrically(void)
{
    /* Half a step is 476.8 ppb: N = 0.4991 at 476, 0.5002 at 477. */
    assert_setting(0, 0, RtcCalr_FromPpb(476));
    assert_setting(1, 511, RtcCalr_FromPpb(477));
    assert_setting(0, 0, RtcCalr_FromPpb(-476));
    assert_setting(0, 1, RtcCalr_FromPpb(-477));
}

/* Every setting survives the trip through ppb. */
void test_every_setting_round_trips(void)
{
    for (int32_t n = RTC_CALR_N_MIN; n <= RTC_CALR_N_MAX; n++) {
        RtcCalr_t c = RtcCalr_FromPulses(n);
        TEST_ASSERT_EQUAL_INT32(n, RtcCalr_Pulses(c));
        RtcCalr_t back = RtcCalr_FromPpb(RtcCalr_ToPpb(c));
        TEST_ASSERT_EQUAL_INT32_MESSAGE(n, RtcCalr_Pulses(back), "N changed through ppb");
        TEST_ASSERT_EQUAL_UINT8(c.calp, back.calp);
        TEST_ASSERT_EQUAL_UINT16(c.calm, back.calm);
    }
}

/* Any in-range request lands within half a step (477.3 ppb at the positive end). */
void test_request_error_is_at_most_half_a_step(void)
{
    for (int32_t ppb = -487090; ppb <= 488519; ppb += 37) {
        int32_t got = RtcCalr_ToPpb(RtcCalr_FromPpb(ppb));
        int32_t err = got - ppb;
        TEST_ASSERT_TRUE_MESSAGE(err >= -478 && err <= 478, "more than half a step off");
    }
}

/* ToPpb against the reference manual's formula, evaluated in double. */
void test_matches_the_reference_manual_formula(void)
{
    const struct { int calp; int calm; } cases[] = {
        {0, 0}, {0, 1}, {0, 8}, {0, 255}, {0, 511}, {1, 0}, {1, 1}, {1, 504}, {1, 256}, {1, 511},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        double p = cases[i].calp * 512.0, m = cases[i].calm;
        double ratio = 1.0 + (p - m) / (1048576.0 + m - p);
        RtcCalr_t c = { (uint8_t)cases[i].calp, (uint16_t)cases[i].calm };
        double got = 1.0 + RtcCalr_ToPpb(c) / 1e9;
        double d = got - ratio;
        TEST_ASSERT_TRUE_MESSAGE(d < 1e-9 && d > -1e-9, "differs from the RM0453 formula");
    }
}

void test_step_constant_is_the_nominal_step(void)
{
    double step = 1e9 / 1048576.0;
    TEST_ASSERT_TRUE(RTC_CALR_STEP_PPB >= step - 1.0 && RTC_CALR_STEP_PPB <= step + 1.0);
}

void test_register_packing(void)
{
    RtcCalr_t c = { 1, 504 };
    TEST_ASSERT_EQUAL_HEX32(0x8000u | 504u, RtcCalr_ToReg(c));
    TEST_ASSERT_EQUAL_HEX32(8u, RtcCalr_ToReg((RtcCalr_t){ 0, 8 }));

    RtcCalr_t back;
    TEST_ASSERT_TRUE(RtcCalr_FromReg(0x8000u | 504u, &back));
    assert_setting(1, 504, back);
    TEST_ASSERT_TRUE(RtcCalr_FromReg(0u, &back));
    assert_setting(0, 0, back);
}

/* CALW8 / CALW16 select a shorter window: the 2^20-pulse maths does not apply. */
void test_register_with_a_short_window_is_refused(void)
{
    RtcCalr_t c;
    TEST_ASSERT_FALSE(RtcCalr_FromReg(1u << 14, &c));
    TEST_ASSERT_FALSE(RtcCalr_FromReg(1u << 13, &c));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_zero_is_no_calibration);
    RUN_TEST(test_fast_rtc_is_slowed_with_calm_alone);
    RUN_TEST(test_slow_rtc_uses_calp_with_calm_512_minus_n);
    RUN_TEST(test_one_step_either_side_of_zero);
    RUN_TEST(test_range_edges);
    RUN_TEST(test_beyond_the_range_saturates);
    RUN_TEST(test_rounds_to_the_nearest_step_symmetrically);
    RUN_TEST(test_every_setting_round_trips);
    RUN_TEST(test_request_error_is_at_most_half_a_step);
    RUN_TEST(test_matches_the_reference_manual_formula);
    RUN_TEST(test_step_constant_is_the_nominal_step);
    RUN_TEST(test_register_packing);
    RUN_TEST(test_register_with_a_short_window_is_refused);
    return UNITY_END();
}
