/*!
 * \file      calr_policy.h
 *
 * \brief     When the drift estimate is worth a new RTC_CALR write (#34).
 *
 * \details   The calibration moves in steps of ~0.954 ppm
 *            (\ref RTC_CALR_STEP_PPB), so the clock keeps a residual
 *            between two writes. A new setting is written when the estimate
 *            is valid and the residual, rate + applied, reaches
 *            \ref CALR_WRITE_THRESHOLD_PPB, 0.75 step:
 *
 *              - above half a step (477 ppb), so noise on the estimate
 *                (0.1-0.2 ppm once converged) cannot flip the setting back
 *                and forth around the middle of two steps;
 *              - under a full step, so the residual a drifting crystal
 *                (temperature) is left with stays under ~0.72 ppm instead
 *                of ~0.95 ppm.
 *
 *            The first write after convergence is not limited to one step:
 *            the target is the setting nearest to cancelling the whole
 *            rate. The policy assumes the estimator was told the setting in
 *            the register (\c current), which is how its residual is made.
 *
 * \author    Simon R.C. Langlais ( Celium )
 */
#ifndef CALR_POLICY_H
#define CALR_POLICY_H

#include <stdbool.h>
#include <stdint.h>
#include "drift_estimator.h"
#include "rtc_calr.h"

/*! Residual that triggers a write: 3/4 of a step, 715 ppb. */
#define CALR_WRITE_THRESHOLD_PPB  ((RTC_CALR_STEP_PPB * 3) / 4)

typedef struct {
    bool      write;    /*!< Write \c calr now. */
    RtcCalr_t calr;     /*!< The setting that best cancels the estimated rate. */
    int32_t   req_ppb;  /*!< The frequency change asked for, ppb (-rate): logged as `req`. */
} CalrDecision_t;

/*!
 * \param   e        The estimate after the latest sample.
 * \param   current  The setting in the RTC_CALR register.
 */
static inline CalrDecision_t CalrPolicy_Decide(const DriftEstimate_t *e, RtcCalr_t current)
{
    CalrDecision_t d;
    d.req_ppb = -e->rate_ppb;
    d.calr    = RtcCalr_FromPpb(d.req_ppb);
    int32_t residual = e->residual_ppb;
    d.write = e->valid
           && (residual >= CALR_WRITE_THRESHOLD_PPB || residual <= -CALR_WRITE_THRESHOLD_PPB)
           && RtcCalr_Pulses(d.calr) != RtcCalr_Pulses(current);
    return d;
}

#endif /* CALR_POLICY_H */
