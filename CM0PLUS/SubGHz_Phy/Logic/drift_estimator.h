/*!
 * \file      drift_estimator.h
 *
 * \brief     Runtime estimate of the RTC's rate offset against its Sync
 *            sender (issue #34, Phase 1).
 *
 * \details   Pure C, integer arithmetic only (the CM0+ has no FPU), no HAL.
 *            Every Sync reception gives one sample: the signed error
 *            err = SyncStamp - expected arrival, in ms (positive: the local
 *            clock is ahead, so it runs fast or was ahead). The estimator
 *            fits a straight line to the unwrapped errors, over a window of
 *            the latest samples, and reports its slope.
 *
 *            Unwrapping removes what the node did to its own clock, so the
 *            line shows the crystal alone:
 *              - a phase shift (\ref DriftEstimator_OnShift) is subtracted
 *                from the samples after it;
 *              - a calibration (\ref DriftEstimator_OnCalr) is integrated
 *                over time and subtracted, so the slope is the rate offset
 *                with no calibration, whatever was applied meanwhile;
 *              - an RTC_SET (\ref DriftEstimator_OnRtcSet) starts a new
 *                segment: the error jumps by an amount not known to the
 *                sub-ms, so the segment gets its own intercept and shares
 *                the slope with the others (the fit is made on the
 *                deviations from each segment's own mean).
 *
 *            Sign conventions, all in one place:
 *              - err, and rate_ppb: positive = the local clock runs ahead
 *                of / faster than the sender's;
 *              - shift_ticks: positive = the clock was advanced (the
 *                RTC_SET `adv` convention; a Tier 2 delay is negative);
 *              - applied_ppb: the frequency change the calibration applies
 *                to the RTC, in the reference manual's sense (see
 *                \ref RtcCalr_ToPpb): negative slows a fast clock.
 *                residual = rate + applied: what the clock still gains.
 *
 *            Time is the monotonic ms of the day clock (day_ms.h,
 *            TIMER_IF_GetMonotonicMs), never the day-wrapping RTC ms. It
 *            wraps at 2^32; calls must be less than 24 days apart.
 *
 *            The estimate is kept as long as the module lives: a return to
 *            CLOCK_COLD does not reset it (the rate belongs to the crystal,
 *            not to the lock). Not persisted across a reboot (Phase 2).
 *
 *            Not thread safe: call from the one context that processes
 *            Sync packets and RTC writes (the CM0+ slot task).
 *
 * \code
 *              ____  ______  _         ___   _   _  __  __
 *             / ___||  ____|| |       |_ _| | | | ||  \/  |
 *            | |    | |__   | |        | |  | | | || |\/| |
 *            | |___ |  __|  | |___    _| |_ | |_| || |  | |
 *             \____||______| \_____| |_____| \___/ |_|  |_|
 *            (C)2025-2026 Celium
 *
 * \endcode
 *
 * \author    Simon R.C. Langlais ( Celium )
 *
 */
#ifndef DRIFT_ESTIMATOR_H
#define DRIFT_ESTIMATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "guard_time_resolver.h"   /* SYNC_RESYNC_THRESHOLD_MS */

/* ---------------------------------------------------------------------------
 * Constants, chosen from the C3-C2 bench of 2026-09-26 (issue #34): 13 cell-0
 * Sync packets over 570 s, rate +8.1 +/- 0.6 ppm, residual sigma 0.36 ms.
 *
 * For n samples spread over a baseline T, white noise of sigma gives a slope
 * standard deviation sigma_b = sigma / sqrt(Stt), with
 * Stt = sum (t - mean t)^2 = n T^2 / 12 for even spacing. The bench figures
 * give 0.36 ms / (570 s x sqrt(13/12)) = 0.6 ppm, which is 0.63 of a
 * calibration step (0.954 ppm): too coarse to decide a one-step CALR write.
 * The target is sigma_b <= 0.35 ppm (a step is then 2.7 sigma), so that a
 * CALR write is rarely a noise artefact and, if it is, costs one step.
 * --------------------------------------------------------------------------- */

/*! Samples kept: the fit window. At the bench cadence (a Sync packet every
 *  ~47 s) it covers ~37 min, ~24 min at one per 30 s, the memory of the estimator (a slow rate change
 *  from temperature is followed with about that lag). 12 bytes each. */
#define DRIFT_WINDOW_SAMPLES     48u

/*! Samples closer than this to the previous one (same segment) are not kept:
 *  they add no independent information on the slope and would shrink the
 *  window to seconds (the cells of one Sync phase are 3 s apart). Under the
 *  Sync phase period (30 s today, so a packet is never lost to the second
 *  rounding of the sample times), over the cell spacing. */
#define DRIFT_MIN_SPACING_S      20u

/*! Minimum samples for a valid estimate: enough degrees of freedom to
 *  estimate the noise (the bench had 13). */
#define DRIFT_MIN_SAMPLES        8u

/*! Minimum baseline, twice the bench's 570 s: at the same cadence it halves
 *  sigma_b to ~0.3 ppm (0.2 ppm with the sample count of that baseline). */
#define DRIFT_MIN_BASELINE_S     1200u

/*! Minimum Stt, s^2: the same bound in terms of information, which also
 *  covers a window made of several RTC_SET segments (each with its own
 *  intercept, so only the spread inside a segment counts). Design noise
 *  0.4 ms (bench 0.36 ms plus margin) and sigma_b 0.35 ppm give
 *  (0.4e-3 / 0.35e-6)^2 = 1.3e6 s^2. */
#define DRIFT_MIN_STT_S2         1300000

/* Errors at or beyond SYNC_RESYNC_THRESHOLD_MS (guard_time_resolver.h; Tier 3
 * for the MAC) are not samples: the node re-anchors instead of drifting. */

/*! Outlier gate: a sample further than this many sigma from the fit's
 *  prediction (sigma widened by the prediction's own uncertainty) is not
 *  kept. */
#define DRIFT_OUTLIER_SIGMA      4u

/*! Floor of the sigma used by the outlier gate, us: the design noise
 *  (bench 0.36 ms plus margin). Also used while the noise is not
 *  estimable yet. */
#define DRIFT_SIGMA_FLOOR_US     400u

/*! Samples of the current segment needed before the gate applies. */
#define DRIFT_OUTLIER_MIN_SEG_N  3u

/*! Consecutive outliers after which the model is taken to have changed
 *  (a phase step not reported through OnShift): the latest sample starts a
 *  new segment instead of being rejected for ever. */
#define DRIFT_OUTLIER_MAX_RUN    3u

typedef struct {
    bool     valid;         /*!< Enough data (see the constants above): rate_ppb may drive a calibration. */
    int32_t  rate_ppb;      /*!< Rate offset to cancel, ppb, no calibration: + = local clock fast. */
    int32_t  residual_ppb;  /*!< rate_ppb + applied_ppb: what the clock still gains with the calibration applied. */
    uint32_t noise_us;      /*!< Sample noise, us (sigma of the fit's residuals); 0 = not estimable yet. */
    uint16_t n;             /*!< Samples in the fit window. */
    uint32_t baseline_s;    /*!< Time between the oldest and newest sample in the window, s. */
} DriftEstimate_t;

typedef enum {
    DRIFT_SAMPLE_ACCEPTED,  /*!< Kept in the window. */
    DRIFT_SAMPLE_TOO_FAR,   /*!< |err| >= SYNC_RESYNC_THRESHOLD_MS: not a sample. */
    DRIFT_SAMPLE_OUTLIER,   /*!< Far from the fit's prediction: rejected. */
    DRIFT_SAMPLE_TOO_SOON   /*!< Closer than DRIFT_MIN_SPACING_S to the previous one: dropped. */
} DriftSampleResult_t;

/*! \brief  Forget everything (boot, tests). A zeroed module is already reset. */
void DriftEstimator_Init(void);

/*!
 * \brief   One Sync reception.
 *
 * \param   t_ms    Monotonic ms of the reception.
 * \param   err_ms  SyncStamp - expected arrival, ms, as logged in SYNC_RX,
 *                  taken before any correction this packet causes.
 *
 * \details Call it before the OnShift of the same packet, so the shift counts
 *          for the next samples and not this one.
 */
DriftSampleResult_t DriftEstimator_AddSample(uint32_t t_ms, int32_t err_ms);

/*!
 * \brief   A phase shift (SHIFTR) was written to the RTC.
 * \param   shift_ticks  RTC ticks (1/4096 s), + = clock advanced, - = delayed.
 *                       Only a shift that was actually written.
 */
void DriftEstimator_OnShift(uint32_t t_ms, int32_t shift_ticks);

/*!
 * \brief   The smooth calibration changed (or was read at boot).
 * \param   applied_ppb  Frequency change now applied to the RTC, ppb,
 *                       RtcCalr_ToPpb() of the register written.
 */
void DriftEstimator_OnCalr(uint32_t t_ms, int32_t applied_ppb);

/*! \brief  The calendar was set (Packet 1, Tier 3): the next sample starts a new segment. */
void DriftEstimator_OnRtcSet(uint32_t t_ms);

/*! \brief  The current estimate. */
DriftEstimate_t DriftEstimator_Get(void);

#endif /* DRIFT_ESTIMATOR_H */
