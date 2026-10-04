/*!
 * \file      drift_estimator.c
 *
 * \brief     Runtime drift estimator: least squares over a window of
 *            unwrapped Sync errors. See drift_estimator.h.
 *
 * \details   The window is recomputed from its samples at each change
 *            (two or three passes over at most DRIFT_WINDOW_SAMPLES), not
 *            updated recursively: at one sample per Sync packet the cost is
 *            negligible, and the result has no accumulated rounding.
 *
 *            Units: time in whole seconds since the first call, error in us.
 *            A slope in us/s is a rate in ppm, so ppb = slope x 1000.
 *            Rounding the sample time to a second moves a sample by at most
 *            0.5 s x rate = 4 us at 8 ppm, far under the 360 us noise.
 *
 * \author    Simon R.C. Langlais ( Celium )
 */
#include "drift_estimator.h"

#include <string.h>

#define N_MAX            DRIFT_WINDOW_SAMPLES
#define PS_PER_TICK      244140625LL   /* 1e12 ps / 4096 ticks, exact */
#define REBASE_LIMIT_US  (1LL << 30)

typedef struct {
    int32_t t_s;    /* seconds since the first call */
    int32_t u_us;   /* unwrapped error - s_u_base_us, us */
    uint8_t seg;    /* segment id (wraps; only equality is used) */
} Sample;

typedef struct {
    uint16_t n;
    uint16_t nruns;
    int64_t  stt;          /* sum over runs of sum (t - run mean)^2, s^2 */
    int64_t  stu;          /* same for (t - mean t)(u - mean u), s.us */
    int32_t  slope_ppb;
    uint32_t noise_us;     /* 0: not estimable */
    uint32_t baseline_s;
    /* The newest run, for the outlier gate. */
    uint8_t  last_seg;
    uint16_t last_n;
    int32_t  last_mean_t;
    int32_t  last_mean_u;
} Fit;

static Sample   s_ring[N_MAX];
static uint16_t s_count;            /* samples held */
static uint16_t s_head;             /* next write position */
static uint8_t  s_seg;              /* current segment id */
static uint8_t  s_reject_run;       /* consecutive outliers */
static bool     s_started;
static uint32_t s_last_t_ms;
static uint64_t s_elapsed_ms;       /* monotonic ms since the first call, 64-bit: no wrap */
static int32_t  s_applied_ppb;
static int64_t  s_unwrap_ps;        /* sum of shifts + integral of applied, ps */
static int64_t  s_u_base_us;
static Fit      s_fit;

/* ---- arithmetic helpers -------------------------------------------------- */

/* num / den rounded to nearest, halves away from zero; den > 0. */
static int64_t round_div(int64_t num, int64_t den)
{
    int64_t half = den / 2;
    return (num >= 0) ? (num + half) / den : -((-num + half) / den);
}

static uint32_t isqrt64(uint64_t x)
{
    uint64_t r = 0, bit = 1ULL << 62;
    while (bit > x) bit >>= 2;
    while (bit != 0) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; }
        else              { r >>= 1; }
        bit >>= 2;
    }
    return (uint32_t)r;
}

static const Sample *at(uint16_t i)
{
    return &s_ring[(s_head + N_MAX - s_count + i) % N_MAX];
}

/* ---- the fit ------------------------------------------------------------- */

/* Run starting at i (consecutive samples of one segment): end and means. */
static uint16_t run_at(uint16_t i, int32_t *mean_t, int32_t *mean_u)
{
    uint8_t seg = at(i)->seg;
    int64_t sum_t = 0, sum_u = 0;
    uint16_t j = i;
    while (j < s_count && at(j)->seg == seg) {
        sum_t += at(j)->t_s;
        sum_u += at(j)->u_us;
        j++;
    }
    *mean_t = (int32_t)round_div(sum_t, j - i);
    *mean_u = (int32_t)round_div(sum_u, j - i);
    return j;
}

static void compute_fit(void)
{
    Fit f;
    memset(&f, 0, sizeof f);
    f.n = s_count;
    if (s_count == 0u) {
        s_fit = f;
        return;
    }
    f.baseline_s = (uint32_t)(at(s_count - 1u)->t_s - at(0)->t_s);

    for (uint16_t i = 0; i < s_count;) {
        int32_t mt, mu;
        uint16_t j = run_at(i, &mt, &mu);
        for (uint16_t k = i; k < j; k++) {
            int64_t dt = at(k)->t_s - mt;
            int64_t du = at(k)->u_us - mu;
            f.stt += dt * dt;
            f.stu += dt * du;
        }
        f.nruns++;
        if (j == s_count) {
            f.last_seg    = at(i)->seg;
            f.last_n      = (uint16_t)(j - i);
            f.last_mean_t = mt;
            f.last_mean_u = mu;
        }
        i = j;
    }

    if (f.stt > 0) {
        f.slope_ppb = (int32_t)round_div(f.stu * 1000, f.stt);

        /* Residuals around the common slope (Q20 us/s keeps the sub-ppm
         * part without leaving 64 bits). */
        int64_t b_q20 = round_div(f.stu * (1 << 20), f.stt);
        uint64_t rss = 0;
        for (uint16_t i = 0; i < s_count;) {
            int32_t mt, mu;
            uint16_t j = run_at(i, &mt, &mu);
            for (uint16_t k = i; k < j; k++) {
                int64_t dt = at(k)->t_s - mt;
                int64_t du = at(k)->u_us - mu;
                int64_t r  = du - ((b_q20 * dt) >> 20);
                rss += (uint64_t)(r * r);
            }
            i = j;
        }
        int32_t dof = (int32_t)f.n - (int32_t)f.nruns - 1;
        if (dof >= 1) {
            f.noise_us = isqrt64(rss / (uint64_t)dof);
        }
    }
    s_fit = f;
}

/* ---- clock and unwrapping ------------------------------------------------ */

/* Move the module's time to t_ms and integrate the applied calibration up to
 * it. A time going backwards (more than half the 32-bit range) counts as 0. */
static void advance(uint32_t t_ms)
{
    if (!s_started) {
        s_started   = true;
        s_last_t_ms = t_ms;
        return;
    }
    uint32_t d = t_ms - s_last_t_ms;
    if (d > 0x7FFFFFFFu) d = 0u;
    s_last_t_ms   = t_ms;
    s_elapsed_ms += d;
    s_unwrap_ps  += (int64_t)s_applied_ppb * (int64_t)d;   /* ppb x ms = ps */
}

/* ---- public API ---------------------------------------------------------- */

void DriftEstimator_Init(void)
{
    memset(s_ring, 0, sizeof s_ring);
    s_count = 0u; s_head = 0u; s_seg = 0u; s_reject_run = 0u;
    s_started = false; s_last_t_ms = 0u; s_elapsed_ms = 0u;
    s_applied_ppb = 0; s_unwrap_ps = 0; s_u_base_us = 0;
    memset(&s_fit, 0, sizeof s_fit);
}

void DriftEstimator_OnShift(uint32_t t_ms, int32_t shift_ticks)
{
    advance(t_ms);
    s_unwrap_ps += (int64_t)shift_ticks * PS_PER_TICK;
}

void DriftEstimator_OnCalr(uint32_t t_ms, int32_t applied_ppb)
{
    advance(t_ms);                 /* integrates the previous value up to t_ms */
    s_applied_ppb = applied_ppb;
}

void DriftEstimator_OnRtcSet(uint32_t t_ms)
{
    advance(t_ms);
    s_seg++;
    s_reject_run = 0u;
}

/* True when the sample (t_s, u_rel) is further from the fit's prediction than
 * DRIFT_OUTLIER_SIGMA sigma, where sigma is widened by the uncertainty of the
 * prediction: sigma^2 x (1 + 1/n + (t - mean t)^2 / Stt). That term is what
 * lets a sample after a long silence through, when the slope error has had
 * time to add up. */
static bool is_outlier(int32_t t_s, int64_t u_rel)
{
    const Fit *f = &s_fit;
    if (f->last_seg != s_seg || f->last_n < DRIFT_OUTLIER_MIN_SEG_N || f->stt <= 0) {
        return false;
    }
    int64_t dt   = (int64_t)t_s - f->last_mean_t;
    int64_t pred = (int64_t)f->last_mean_u + round_div(f->stu * dt, f->stt);
    int64_t dev  = u_rel - pred;

    int64_t sigma = (f->noise_us > DRIFT_SIGMA_FLOOR_US) ? (int64_t)f->noise_us
                                                         : (int64_t)DRIFT_SIGMA_FLOOR_US;
    /* (1 + 1/n + dt^2/Stt) in Q8 */
    int64_t widen_q8 = 256 + 256 / f->last_n + (dt * dt * 256) / f->stt;
    int64_t k2s2     = (int64_t)DRIFT_OUTLIER_SIGMA * DRIFT_OUTLIER_SIGMA * sigma * sigma;
    return dev * dev * 256 > k2s2 * widen_q8;
}

DriftSampleResult_t DriftEstimator_AddSample(uint32_t t_ms, int32_t err_ms)
{
    return DriftEstimator_AddSampleUs(t_ms, err_ms * 1000);
}

DriftSampleResult_t DriftEstimator_AddSampleUs(uint32_t t_ms, int32_t err_us)
{
    advance(t_ms);

    if (err_us >= (int32_t)SYNC_RESYNC_THRESHOLD_MS * 1000 || err_us <= -(int32_t)SYNC_RESYNC_THRESHOLD_MS * 1000) {
        return DRIFT_SAMPLE_TOO_FAR;
    }

    int32_t t_s = (int32_t)((s_elapsed_ms + 500u) / 1000u);
    /* Unwrapped error: what the crystal alone did (see the header). */
    int64_t raw_us = (int64_t)err_us - round_div(s_unwrap_ps, 1000000);
    if (s_count == 0u) {
        s_u_base_us = raw_us;
    }
    int64_t u_rel = raw_us - s_u_base_us;

    if (s_count > 0u) {
        const Sample *last = at(s_count - 1u);
        if (last->seg == s_seg && t_s - last->t_s < (int32_t)DRIFT_MIN_SPACING_S) {
            return DRIFT_SAMPLE_TOO_SOON;
        }
    }

    if (is_outlier(t_s, u_rel)) {
        if (++s_reject_run < DRIFT_OUTLIER_MAX_RUN) {
            return DRIFT_SAMPLE_OUTLIER;
        }
        s_seg++;            /* the model changed: new intercept, same slope */
    }
    s_reject_run = 0u;

    /* Months at high rate could push u past 32 bits: rebase the window. */
    if (u_rel > REBASE_LIMIT_US || u_rel < -REBASE_LIMIT_US) {
        for (uint16_t i = 0; i < s_count; i++) {
            s_ring[(s_head + N_MAX - s_count + i) % N_MAX].u_us -= (int32_t)u_rel;
        }
        s_u_base_us += u_rel;
        u_rel = 0;
    }

    Sample *w = &s_ring[s_head];
    w->t_s  = t_s;
    w->u_us = (int32_t)u_rel;
    w->seg  = s_seg;
    s_head  = (uint16_t)((s_head + 1u) % N_MAX);
    if (s_count < N_MAX) s_count++;

    compute_fit();
    return DRIFT_SAMPLE_ACCEPTED;
}

DriftEstimate_t DriftEstimator_Get(void)
{
    const Fit *f = &s_fit;
    DriftEstimate_t e;
    e.rate_ppb     = f->slope_ppb;
    e.residual_ppb = f->slope_ppb + s_applied_ppb;
    e.noise_us     = f->noise_us;
    e.n            = f->n;
    e.baseline_s   = f->baseline_s;
    e.valid        = (f->n >= DRIFT_MIN_SAMPLES)
                  && (f->baseline_s >= DRIFT_MIN_BASELINE_S)
                  && (f->stt >= DRIFT_MIN_STT_S2);
    return e;
}
