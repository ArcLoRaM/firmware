#include "unity.h"
#include <math.h>
#include <stdint.h>
#include "drift_estimator.h"
#include "rtc_calr.h"

/*
 * Drift estimator on synthetic series (issue #34).
 *
 * The simulated local clock is err(t) = phase + (rate + calibration) x t, in
 * us, against its sender. Each Sync reception reads it as the firmware does:
 * the SyncStamp is floored to the millisecond (GetTimerTicks), on top of a
 * small gaussian jitter. Noise: jitter sigma 215 us + floor quantisation
 * (289 us) = 0.36 ms, the bench figure of 2026-09-26.
 *
 * Convergence bound asserted below, and why. For white noise sigma and a
 * window of Stt = sum (t - mean t)^2, the slope has sigma_b = sigma/sqrt(Stt).
 *  - At the validity gate (T >= 1200 s, one packet per 47 s, ~27 samples,
 *    Stt ~ 3.6e6 s^2): sigma_b ~ 0.19 ppm. One calibration step is
 *    0.954 ppm, i.e. 5 sigma: the first valid estimate is within one step.
 *  - With the window full (48 samples, Stt ~ 2e7 s^2): sigma_b ~ 0.08 ppm,
 *    so 0.4 ppm is 5 sigma.
 * Each bound is checked over 200 seeds.
 */

#define SEEDS            200
#define PERIOD_S         47      /* bench cadence: a Sync packet per ~47 s */
#define SIGMA_JITTER_US  215.0
#define STEP_PPB         954

void setUp(void)    { DriftEstimator_Init(); }
void tearDown(void) {}

/* ---- simulation ---------------------------------------------------------- */

typedef struct {
    double   t_s;
    double   phase_us;
    double   rate_ppm;
    double   cal_ppm;
    uint32_t t0_ms;
    uint32_t rng;
} Sim;

static double uniform(Sim *s)
{
    s->rng ^= s->rng << 13; s->rng ^= s->rng >> 17; s->rng ^= s->rng << 5;
    return (double)s->rng / 4294967296.0;
}

static double gauss(Sim *s)
{
    double a = 0;
    for (int i = 0; i < 12; i++) a += uniform(s);
    return a - 6.0;
}

static Sim sim_make(uint32_t seed, double rate_ppm)
{
    Sim s = { 0, 0, rate_ppm, 0, 0, seed * 2654435761u + 12345u };
    s.phase_us = uniform(&s) * 4000.0 - 2000.0;
    for (int i = 0; i < 4; i++) uniform(&s);
    return s;
}

static uint32_t sim_ms(const Sim *s) { return s->t0_ms + (uint32_t)(s->t_s * 1000.0); }

static void sim_wait(Sim *s, double dt_s)
{
    s->t_s += dt_s;
    s->phase_us += (s->rate_ppm + s->cal_ppm) * dt_s;     /* ppm x s = us */
}

static int32_t sim_err_ms(Sim *s, double sigma_us)
{
    return (int32_t)floor((s->phase_us + gauss(s) * sigma_us) / 1000.0);
}

static DriftSampleResult_t sim_sample_err(Sim *s, int32_t err_ms)
{
    return DriftEstimator_AddSample(sim_ms(s), err_ms);
}

static DriftSampleResult_t sim_sample(Sim *s)
{
    return sim_sample_err(s, sim_err_ms(s, SIGMA_JITTER_US));
}

/* One packet period later, one sample. */
static DriftSampleResult_t sim_step(Sim *s)
{
    sim_wait(s, PERIOD_S);
    return sim_sample(s);
}

static void sim_steps(Sim *s, int n)
{
    for (int i = 0; i < n; i++) (void)sim_step(s);
}

/* The MAC's Tier 2: a measured error of 8 ms or more is shifted away. */
static void sim_shift_ticks(Sim *s, int32_t ticks)
{
    s->phase_us += ticks * 244.140625;
    DriftEstimator_OnShift(sim_ms(s), ticks);
}

static void sim_set_cal(Sim *s, int32_t ppb)
{
    s->cal_ppm = ppb / 1000.0;
    DriftEstimator_OnCalr(sim_ms(s), ppb);
}

static void sim_rtc_set(Sim *s, double new_phase_us)
{
    s->phase_us = new_phase_us;
    DriftEstimator_OnRtcSet(sim_ms(s));
}

static int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

/* ---- convergence --------------------------------------------------------- */

/* The first valid estimate is within one calibration step of the true rate. */
void test_first_valid_estimate_is_within_one_step(void)
{
    double sum_sq = 0;
    int32_t worst = 0;
    for (int seed = 1; seed <= SEEDS; seed++) {
        DriftEstimator_Init();
        Sim s = sim_make((uint32_t)seed, 8.1);
        int samples = 0;
        DriftEstimate_t e = DriftEstimator_Get();
        while (!e.valid && samples < 100) {
            sim_step(&s);
            samples++;
            e = DriftEstimator_Get();
        }
        TEST_ASSERT_TRUE_MESSAGE(e.valid, "never valid");
        TEST_ASSERT_TRUE_MESSAGE(e.baseline_s >= DRIFT_MIN_BASELINE_S, "valid under the minimum baseline");
        TEST_ASSERT_TRUE_MESSAGE(e.baseline_s < DRIFT_MIN_BASELINE_S + 2 * PERIOD_S, "valid late");
        int32_t err = abs32(e.rate_ppb - 8100);
        if (err > worst) worst = err;
        sum_sq += (double)err * err;
    }
    TEST_ASSERT_TRUE_MESSAGE(worst < STEP_PPB, "first valid estimate more than one step off");
    TEST_ASSERT_TRUE_MESSAGE(sqrt(sum_sq / SEEDS) < 300.0, "rms error at the gate above 0.3 ppm");
}

/* The Sync phase period of the bench schedule: a packet every 30 s. At the
 * gate Stt ~ 4.8e6 s^2, sigma_b ~ 0.16 ppm; one step is 6 sigma. */
void test_first_valid_estimate_at_the_30_s_sync_period(void)
{
    int32_t worst = 0;
    for (int seed = 1; seed <= SEEDS; seed++) {
        DriftEstimator_Init();
        Sim s = sim_make((uint32_t)seed, 8.1);
        int samples = 0;
        DriftEstimate_t e = DriftEstimator_Get();
        while (!e.valid && samples < 200) {
            sim_wait(&s, 30.0);
            TEST_ASSERT_EQUAL(DRIFT_SAMPLE_ACCEPTED, sim_sample(&s));   /* none lost to the spacing rule */
            samples++;
            e = DriftEstimator_Get();
        }
        TEST_ASSERT_TRUE(e.valid);
        TEST_ASSERT_TRUE(e.baseline_s >= DRIFT_MIN_BASELINE_S && e.baseline_s <= DRIFT_MIN_BASELINE_S + 30);
        int32_t err = abs32(e.rate_ppb - 8100);
        if (err > worst) worst = err;
    }
    TEST_ASSERT_TRUE_MESSAGE(worst < STEP_PPB, "first valid estimate more than one step off");
}

void test_estimate_with_a_full_window_is_within_400_ppb(void)
{
    for (int seed = 1; seed <= SEEDS; seed++) {
        DriftEstimator_Init();
        Sim s = sim_make((uint32_t)seed, 8.1);
        sim_steps(&s, 150);
        DriftEstimate_t e = DriftEstimator_Get();
        TEST_ASSERT_TRUE(e.valid);
        TEST_ASSERT_EQUAL_UINT16(DRIFT_WINDOW_SAMPLES, e.n);
        TEST_ASSERT_TRUE_MESSAGE(abs32(e.rate_ppb - 8100) < 400, "full-window estimate above 0.4 ppm off");
        TEST_ASSERT_EQUAL_INT32(e.rate_ppb, e.residual_ppb);   /* nothing applied */
    }
}

/* The sign is the sign of the offset: positive = local clock fast. */
void test_a_slow_clock_gives_a_negative_rate(void)
{
    Sim s = sim_make(7, -5.3);
    sim_steps(&s, 100);
    DriftEstimate_t e = DriftEstimator_Get();
    TEST_ASSERT_TRUE(e.valid);
    TEST_ASSERT_TRUE(abs32(e.rate_ppb + 5300) < 400);
}

/* What the bench had (13 packets over 570 s) is not enough to drive a CALR. */
void test_the_bench_baseline_is_not_valid(void)
{
    Sim s = sim_make(3, 8.1);
    sim_sample(&s);
    sim_steps(&s, 12);
    DriftEstimate_t e = DriftEstimator_Get();
    TEST_ASSERT_EQUAL_UINT16(13, e.n);
    TEST_ASSERT_EQUAL_UINT32(12 * PERIOD_S, e.baseline_s);
    TEST_ASSERT_FALSE(e.valid);
    TEST_ASSERT_TRUE_MESSAGE(abs32(e.rate_ppb - 8100) < 2500, "bench-length estimate absurd");   /* ~0.6 ppm sigma */
}

void test_noise_estimate_is_the_bench_sigma(void)
{
    double sum = 0;
    for (int seed = 1; seed <= 50; seed++) {
        DriftEstimator_Init();
        Sim s = sim_make((uint32_t)seed, 8.1);
        sim_steps(&s, 100);
        sum += DriftEstimator_Get().noise_us;
    }
    double mean = sum / 50;
    TEST_ASSERT_TRUE_MESSAGE(mean > 300.0 && mean < 420.0, "noise not near 0.36 ms");
}

void test_nothing_before_any_sample(void)
{
    DriftEstimate_t e = DriftEstimator_Get();
    TEST_ASSERT_FALSE(e.valid);
    TEST_ASSERT_EQUAL_UINT16(0, e.n);
    TEST_ASSERT_EQUAL_INT32(0, e.rate_ppb);
    TEST_ASSERT_EQUAL_UINT32(0, e.baseline_s);
}

void test_init_forgets_everything(void)
{
    Sim s = sim_make(1, 8.1);
    sim_set_cal(&s, -3000);
    sim_steps(&s, 60);
    DriftEstimator_Init();
    DriftEstimate_t e = DriftEstimator_Get();
    TEST_ASSERT_EQUAL_UINT16(0, e.n);
    TEST_ASSERT_FALSE(e.valid);
    TEST_ASSERT_EQUAL_INT32(0, e.residual_ppb);   /* the applied calibration is forgotten too */
}

/* ---- actuation: shifts, CALR, RTC_SET ------------------------------------ */

/* 100 ppm gains 4.7 ms per packet: Tier 2 shifts a clock away every other
 * packet. The shifts leave the slope alone. */
void test_phase_shifts_do_not_restart_the_slope(void)
{
    for (int seed = 1; seed <= 50; seed++) {
        DriftEstimator_Init();
        Sim s = sim_make((uint32_t)seed, 100.0);
        int shifts = 0;
        for (int i = 0; i < 120; i++) {
            sim_wait(&s, PERIOD_S);
            int32_t err = sim_err_ms(&s, SIGMA_JITTER_US);
            sim_sample_err(&s, err);
            if (abs32(err) >= 8) {
                /* err > 0: delayed (negative), err < 0: advanced. */
                sim_shift_ticks(&s, (int32_t)lround(-err * 4.096));
                shifts++;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(shifts > 30, "scenario made no shifts");
        DriftEstimate_t e = DriftEstimator_Get();
        TEST_ASSERT_TRUE(e.valid);
        TEST_ASSERT_TRUE_MESSAGE(abs32(e.rate_ppb - 100000) < 500, "shifts bent the slope");
    }
}

/* The estimate is the rate with no calibration, whatever is applied: a CALR
 * change moves the residual, not the rate. */
void test_a_calr_change_moves_the_residual_not_the_rate(void)
{
    int32_t applied = RtcCalr_ToPpb(RtcCalr_FromPpb(-8100));     /* -7629 */
    TEST_ASSERT_EQUAL_INT32(-7629, applied);
    for (int seed = 1; seed <= 50; seed++) {
        DriftEstimator_Init();
        Sim s = sim_make((uint32_t)seed, 8.1);
        sim_steps(&s, 60);
        sim_set_cal(&s, applied);
        sim_steps(&s, 20);                       /* window: 28 uncalibrated + 20 calibrated */
        DriftEstimate_t mid = DriftEstimator_Get();
        TEST_ASSERT_TRUE_MESSAGE(abs32(mid.rate_ppb - 8100) < 700, "rate followed the calibration (mixed window)");
        sim_steps(&s, 60);                       /* window fully calibrated */
        DriftEstimate_t e = DriftEstimator_Get();
        TEST_ASSERT_TRUE_MESSAGE(abs32(e.rate_ppb - 8100) < 400, "rate followed the calibration");
        /* The clock still gains 8100 - 7629 = 471 ppb. */
        TEST_ASSERT_TRUE_MESSAGE(abs32(e.residual_ppb - 471) < 400, "residual wrong");
        TEST_ASSERT_EQUAL_INT32(e.rate_ppb + applied, e.residual_ppb);
    }
}

void test_a_second_calr_change_and_back_to_none(void)
{
    Sim s = sim_make(11, 8.1);
    sim_steps(&s, 40);
    sim_set_cal(&s, -4000);
    sim_steps(&s, 50);
    sim_set_cal(&s, -7629);
    sim_steps(&s, 50);
    sim_set_cal(&s, 0);
    sim_steps(&s, 60);
    DriftEstimate_t e = DriftEstimator_Get();
    TEST_ASSERT_TRUE(abs32(e.rate_ppb - 8100) < 400);
    TEST_ASSERT_EQUAL_INT32(e.rate_ppb, e.residual_ppb);
}

/* An RTC_SET (Packet 1, Tier 3) jumps the error by a large unknown amount:
 * the segment gets a new intercept and the slope carries on. */
void test_an_rtc_set_breaks_the_segment_and_keeps_the_slope(void)
{
    for (int seed = 1; seed <= 100; seed++) {
        DriftEstimator_Init();
        Sim s = sim_make((uint32_t)seed, 8.1);
        sim_steps(&s, 70);                        /* ~27 ms gained by now */
        sim_rtc_set(&s, 0.4 * (uniform(&s) - 0.5) * 1000.0);
        TEST_ASSERT_EQUAL(DRIFT_SAMPLE_ACCEPTED, sim_step(&s));     /* not an outlier, not too soon */
        sim_steps(&s, 70);
        DriftEstimate_t e = DriftEstimator_Get();
        TEST_ASSERT_TRUE(e.valid);
        TEST_ASSERT_TRUE_MESSAGE(abs32(e.rate_ppb - 8100) < 400, "RTC_SET bent the slope");
    }
}

/* The slope is kept over a set even when little is learnt after it. */
void test_the_rate_is_available_right_after_a_set(void)
{
    Sim s = sim_make(5, 8.1);
    sim_steps(&s, 70);
    DriftEstimate_t before = DriftEstimator_Get();
    sim_rtc_set(&s, 100.0);
    sim_step(&s);
    sim_step(&s);
    DriftEstimate_t after = DriftEstimator_Get();
    TEST_ASSERT_TRUE(after.valid);
    TEST_ASSERT_TRUE_MESSAGE(abs32(after.rate_ppb - before.rate_ppb) < 300, "rate lost at the set");
}

/* Only the spread inside a segment says anything about the slope: two short
 * segments a long time apart are a long baseline without information. */
void test_short_segments_far_apart_are_not_valid(void)
{
    Sim s = sim_make(9, 8.1);
    sim_sample(&s);
    sim_steps(&s, 5);
    sim_wait(&s, 1500);
    sim_rtc_set(&s, 0.0);
    sim_sample(&s);
    sim_steps(&s, 5);
    DriftEstimate_t e = DriftEstimator_Get();
    TEST_ASSERT_EQUAL_UINT16(12, e.n);
    TEST_ASSERT_TRUE(e.baseline_s >= DRIFT_MIN_BASELINE_S);
    TEST_ASSERT_FALSE(e.valid);
}

/* ---- rejection ------------------------------------------------------------ */

void test_errors_at_the_resync_threshold_are_not_samples(void)
{
    Sim s = sim_make(2, 8.1);
    sim_steps(&s, 30);
    uint16_t n = DriftEstimator_Get().n;
    TEST_ASSERT_EQUAL(DRIFT_SAMPLE_TOO_FAR, (sim_wait(&s, PERIOD_S), sim_sample_err(&s, (int32_t)SYNC_RESYNC_THRESHOLD_MS)));
    TEST_ASSERT_EQUAL(DRIFT_SAMPLE_TOO_FAR, (sim_wait(&s, PERIOD_S), sim_sample_err(&s, -(int32_t)SYNC_RESYNC_THRESHOLD_MS)));
    TEST_ASSERT_EQUAL(DRIFT_SAMPLE_TOO_FAR, (sim_wait(&s, PERIOD_S), sim_sample_err(&s, 1500)));
    TEST_ASSERT_EQUAL_UINT16(n, DriftEstimator_Get().n);
    /* Not an outlier run either: a good sample is still accepted. */
    TEST_ASSERT_EQUAL(DRIFT_SAMPLE_ACCEPTED, sim_step(&s));
}

void test_isolated_outliers_are_rejected_and_do_not_bias(void)
{
    for (int seed = 1; seed <= 100; seed++) {
        DriftEstimator_Init();
        Sim s = sim_make((uint32_t)seed, 8.1);
        sim_steps(&s, 20);
        int rejected = 0;
        for (int i = 0; i < 100; i++) {
            sim_wait(&s, PERIOD_S);
            int32_t err = sim_err_ms(&s, SIGMA_JITTER_US);
            if (i % 9 == 4) {
                err += (i % 2) ? 5 : -6;          /* a bad stamp */
                if (sim_sample_err(&s, err) == DRIFT_SAMPLE_OUTLIER) rejected++;
            } else {
                sim_sample_err(&s, err);
            }
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(11, rejected, "outliers not all rejected");
        DriftEstimate_t e = DriftEstimator_Get();
        TEST_ASSERT_TRUE_MESSAGE(abs32(e.rate_ppb - 8100) < 400, "outliers biased the slope");
    }
}

/* A step the estimator was never told about (it should be told, but a
 * missing report must not lock the estimator out): three outliers in a row
 * start a new segment, and the slope survives. */
void test_an_unreported_step_starts_a_new_segment(void)
{
    for (int seed = 1; seed <= 50; seed++) {
        DriftEstimator_Init();
        Sim s = sim_make((uint32_t)seed, 8.1);
        sim_steps(&s, 40);
        s.phase_us += 20000.0;                   /* +20 ms, no OnShift / OnRtcSet */
        TEST_ASSERT_EQUAL(DRIFT_SAMPLE_OUTLIER, sim_step(&s));
        TEST_ASSERT_EQUAL(DRIFT_SAMPLE_OUTLIER, sim_step(&s));
        TEST_ASSERT_EQUAL(DRIFT_SAMPLE_ACCEPTED, sim_step(&s));
        sim_steps(&s, 70);
        DriftEstimate_t e = DriftEstimator_Get();
        TEST_ASSERT_TRUE(e.valid);
        TEST_ASSERT_TRUE_MESSAGE(abs32(e.rate_ppb - 8100) < 400, "step bent the slope");
    }
}

void test_samples_closer_than_the_minimum_spacing_are_dropped(void)
{
    Sim s = sim_make(4, 8.1);
    int accepted = 0, dropped = 0;
    for (int i = 0; i < 100; i++) {            /* one every 10 s */
        DriftSampleResult_t r = sim_sample(&s);
        if (r == DRIFT_SAMPLE_ACCEPTED) accepted++;
        if (r == DRIFT_SAMPLE_TOO_SOON) dropped++;
        sim_wait(&s, 10.0);
    }
    TEST_ASSERT_EQUAL_INT(50, accepted);        /* t = 0, 20, ..., 980 */
    TEST_ASSERT_EQUAL_INT(50, dropped);
    TEST_ASSERT_EQUAL_UINT16(DRIFT_WINDOW_SAMPLES, DriftEstimator_Get().n);
}

/* ---- gaps, forgetting, limits --------------------------------------------- */

/* CLOCK_COLD: nothing is reset. After hours of silence and a Packet 1 set
 * the rate is the one learnt before, at once. */
void test_the_rate_survives_a_cold_gap(void)
{
    Sim s = sim_make(6, 8.1);
    sim_steps(&s, 80);
    DriftEstimate_t before = DriftEstimator_Get();
    TEST_ASSERT_TRUE(before.valid);

    sim_wait(&s, 3 * 3600);                      /* no Sync packet for 3 h */
    DriftEstimate_t during = DriftEstimator_Get();
    TEST_ASSERT_TRUE(during.valid);
    TEST_ASSERT_EQUAL_INT32(before.rate_ppb, during.rate_ppb);

    sim_rtc_set(&s, 800.0);                      /* Packet 1 */
    sim_steps(&s, 3);
    DriftEstimate_t after = DriftEstimator_Get();
    TEST_ASSERT_TRUE(after.valid);
    TEST_ASSERT_TRUE_MESSAGE(abs32(after.rate_ppb - 8100) < 400, "rate lost over the gap");
}

/* After a long silence the slope error has had time to add up: the first
 * sample is judged against a correspondingly wider prediction. 1.5 ppm: at
 * 8 ppm, three hours would be past the resync threshold (a Tier 3 sample). */
void test_the_first_sample_after_a_long_silence_is_not_an_outlier(void)
{
    for (int seed = 1; seed <= SEEDS; seed++) {
        DriftEstimator_Init();
        Sim s = sim_make((uint32_t)seed, 1.5);
        sim_steps(&s, 60);
        sim_wait(&s, 3 * 3600);
        sim_wait(&s, PERIOD_S);
        TEST_ASSERT_EQUAL(DRIFT_SAMPLE_ACCEPTED, sim_sample(&s));
    }
}

/* The window follows a rate change (temperature): 48 samples later the old
 * rate is gone. */
void test_a_rate_change_is_followed_by_the_window(void)
{
    Sim s = sim_make(8, 8.0);
    sim_steps(&s, 100);
    s.rate_ppm = 4.0;
    sim_steps(&s, 60);
    DriftEstimate_t e = DriftEstimator_Get();
    TEST_ASSERT_TRUE_MESSAGE(abs32(e.rate_ppb - 4000) < 400, "old rate still in the estimate");
}

/* The monotonic ms counter wraps at 2^32 (49.7 days): only differences count. */
void test_the_millisecond_counter_may_wrap(void)
{
    Sim s = sim_make(12, 8.1);
    s.t0_ms = 0xFFFFFFFFu - 600000u;             /* wraps 10 min in */
    sim_steps(&s, 100);
    DriftEstimate_t e = DriftEstimator_Get();
    TEST_ASSERT_TRUE(e.valid);
    TEST_ASSERT_TRUE(abs32(e.rate_ppb - 8100) < 400);
    TEST_ASSERT_TRUE(e.baseline_s > 2000);
}

/* 28 days at 1000 ppm (far worse than any crystal), shifted away at every
 * packet: the unwrapped error reaches 2400 s, past what 32 bits of us hold,
 * and the window is rebased to stay exact. */
void test_a_month_of_large_shifts_stays_exact(void)
{
    Sim s = sim_make(13, 1000.0);
    for (int i = 0; i < 80000; i++) {            /* 27.8 days at 30 s */
        sim_wait(&s, 30.0);
        int32_t err = sim_err_ms(&s, SIGMA_JITTER_US);
        sim_sample_err(&s, err);
        if (abs32(err) >= 8) {
            sim_shift_ticks(&s, (int32_t)lround(-err * 4.096));
        }
    }
    DriftEstimate_t e = DriftEstimator_Get();
    TEST_ASSERT_TRUE(e.valid);
    TEST_ASSERT_TRUE_MESSAGE(abs32(e.rate_ppb - 1000000) < 500, "rate lost over a month");
}

/* ---- errors in microseconds (issue #82) ----------------------------------- */

/* The stamp in RTC ticks: floor to a tick (244.14 us) instead of to a ms. */
static int32_t sim_err_us(Sim *s, double sigma_us)
{
    double x = s->phase_us + gauss(s) * sigma_us;
    return (int32_t)lround(floor(x / 244.140625) * 244.140625);
}

static DriftSampleResult_t sim_sample_us_err(Sim *s, int32_t err_us)
{
    return DriftEstimator_AddSampleUs(sim_ms(s), err_us);
}

/* A perfect line of 8.1 ppm, read to the us: the sub-ms part is kept, so the
 * rate is exact and the noise nil. The ms entry point could not do this. */
void test_sub_ms_errors_are_kept(void)
{
    for (int i = 0; i < 40; i++) {
        uint32_t t_ms = (uint32_t)i * 47000u;
        int32_t  err_us = (int32_t)lround(8.1 * (t_ms / 1000.0));        /* ppm x s = us */
        DriftEstimator_AddSampleUs(t_ms, err_us + 1500);                  /* 1.5 ms constant offset */
    }
    DriftEstimate_t e = DriftEstimator_Get();
    TEST_ASSERT_TRUE(e.valid);
    TEST_ASSERT_TRUE_MESSAGE(abs32(e.rate_ppb - 8100) < 20, "sub-ms line not recovered");
    /* Not exactly 0: the window's mean time is rounded to a whole second, which leaves
     * rate x 0.5 s = 4 us in the residuals (8.1 ppm), against ~230 us of real noise. */
    TEST_ASSERT_TRUE_MESSAGE(e.noise_us < 10u, "a perfect line has (almost) no noise");
}

/* The same series through the ms entry point is quantised to the ms. */
void test_the_ms_entry_point_is_the_us_one_times_a_thousand(void)
{
    DriftEstimator_AddSample(0u, 7);
    DriftEstimator_AddSample(47000u, 8);
    DriftEstimate_t a = DriftEstimator_Get();
    DriftEstimator_Init();
    DriftEstimator_AddSampleUs(0u, 7000);
    DriftEstimator_AddSampleUs(47000u, 8000);
    DriftEstimate_t b = DriftEstimator_Get();
    TEST_ASSERT_EQUAL_INT32(a.rate_ppb, b.rate_ppb);
    TEST_ASSERT_EQUAL_UINT16(a.n, b.n);
}

void test_errors_at_the_resync_threshold_are_not_samples_in_us(void)
{
    Sim s = sim_make(2, 8.1);
    sim_steps(&s, 10);
    uint16_t n = DriftEstimator_Get().n;
    sim_wait(&s, PERIOD_S);
    TEST_ASSERT_EQUAL(DRIFT_SAMPLE_TOO_FAR, sim_sample_us_err(&s, (int32_t)SYNC_RESYNC_THRESHOLD_MS * 1000));
    sim_wait(&s, PERIOD_S);
    TEST_ASSERT_EQUAL(DRIFT_SAMPLE_TOO_FAR, sim_sample_us_err(&s, -(int32_t)SYNC_RESYNC_THRESHOLD_MS * 1000));
    sim_wait(&s, PERIOD_S);
    TEST_ASSERT_NOT_EQUAL(DRIFT_SAMPLE_TOO_FAR, sim_sample_us_err(&s, (int32_t)SYNC_RESYNC_THRESHOLD_MS * 1000 - 1));
    /* the two at the threshold are TOO_FAR; the one just under it is a Tier 2 error, far from the
     * fit, so an outlier: none of the three entered the window */
    TEST_ASSERT_EQUAL_UINT16(n, DriftEstimator_Get().n);
}

/* The gain, at the same packets: the stamp in ticks has a noise of about
 * 0.23 ms (0.21 ms jitter + 0.07 ms of tick) against the 0.36 ms of the ms
 * stamp. At the validity gate sigma_b = sigma / sqrt(Stt) = 0.23e-3 / 1900 s =
 * 0.12 ppm, so the rms error over 200 seeds must be under 0.16 ppm (it was
 * 0.18 ppm with ms) and no seed further than one step (0.954 ppm). */
void test_ticks_cut_the_rate_error_at_the_gate(void)
{
    double sum_sq = 0;
    int32_t worst = 0;
    for (int seed = 1; seed <= SEEDS; seed++) {
        DriftEstimator_Init();
        Sim s = sim_make((uint32_t)seed, 8.1);
        DriftEstimate_t e = DriftEstimator_Get();
        int n = 0;
        while (!e.valid && n < 100) {
            sim_wait(&s, PERIOD_S);
            sim_sample_us_err(&s, sim_err_us(&s, SIGMA_JITTER_US));
            n++;
            e = DriftEstimator_Get();
        }
        TEST_ASSERT_TRUE(e.valid);
        int32_t err = abs32(e.rate_ppb - 8100);
        if (err > worst) worst = err;
        sum_sq += (double)err * err;
    }
    TEST_ASSERT_TRUE_MESSAGE(worst < STEP_PPB, "a first valid estimate more than one step off");
    TEST_ASSERT_TRUE_MESSAGE(sqrt(sum_sq / SEEDS) < 160.0, "rms error at the gate above 0.16 ppm");
}

void test_the_error_threshold_is_the_mac_resync_threshold(void)
{
    TEST_ASSERT_EQUAL_UINT32(MAX_GUARD_TIME_MS, SYNC_RESYNC_THRESHOLD_MS);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_valid_estimate_is_within_one_step);
    RUN_TEST(test_first_valid_estimate_at_the_30_s_sync_period);
    RUN_TEST(test_estimate_with_a_full_window_is_within_400_ppb);
    RUN_TEST(test_a_slow_clock_gives_a_negative_rate);
    RUN_TEST(test_the_bench_baseline_is_not_valid);
    RUN_TEST(test_noise_estimate_is_the_bench_sigma);
    RUN_TEST(test_nothing_before_any_sample);
    RUN_TEST(test_init_forgets_everything);
    RUN_TEST(test_phase_shifts_do_not_restart_the_slope);
    RUN_TEST(test_a_calr_change_moves_the_residual_not_the_rate);
    RUN_TEST(test_a_second_calr_change_and_back_to_none);
    RUN_TEST(test_an_rtc_set_breaks_the_segment_and_keeps_the_slope);
    RUN_TEST(test_the_rate_is_available_right_after_a_set);
    RUN_TEST(test_short_segments_far_apart_are_not_valid);
    RUN_TEST(test_errors_at_the_resync_threshold_are_not_samples);
    RUN_TEST(test_isolated_outliers_are_rejected_and_do_not_bias);
    RUN_TEST(test_an_unreported_step_starts_a_new_segment);
    RUN_TEST(test_samples_closer_than_the_minimum_spacing_are_dropped);
    RUN_TEST(test_the_rate_survives_a_cold_gap);
    RUN_TEST(test_the_first_sample_after_a_long_silence_is_not_an_outlier);
    RUN_TEST(test_a_rate_change_is_followed_by_the_window);
    RUN_TEST(test_the_millisecond_counter_may_wrap);
    RUN_TEST(test_a_month_of_large_shifts_stays_exact);
    RUN_TEST(test_sub_ms_errors_are_kept);
    RUN_TEST(test_the_ms_entry_point_is_the_us_one_times_a_thousand);
    RUN_TEST(test_errors_at_the_resync_threshold_are_not_samples_in_us);
    RUN_TEST(test_ticks_cut_the_rate_error_at_the_gate);
    RUN_TEST(test_the_error_threshold_is_the_mac_resync_threshold);
    return UNITY_END();
}
