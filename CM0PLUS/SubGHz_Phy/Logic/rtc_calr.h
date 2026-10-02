/*!
 * \file      rtc_calr.h
 *
 * \brief     Conversion between a frequency correction in ppb and the RTC
 *            smooth calibration register (CALP, CALM[8:0], 32 s window).
 *
 * \details   RM0453, smooth digital calibration. Over a 32 s cycle of
 *            2^20 calibrated pulses, CALP = 1 adds 512 RTCCLK pulses and
 *            CALM masks CALM of them, so with
 *
 *                N = CALP x 512 - CALM        N in [-511, +512]
 *
 *            the calibrated frequency is
 *
 *                F_cal = F_rtcclk x (1 + N / (2^20 - N)) = F_rtcclk x 2^20 / (2^20 - N)
 *
 *            (the reference manual writes the same ratio as
 *            1 + (CALP x 512 - CALM) / (2^20 + CALM - CALP x 512)).
 *            It gives the documented range, -487.1 ppm (N = -511) to
 *            +488.5 ppm (N = +512), and steps of about 0.954 ppm (1e9 / 2^20,
 *            slightly more at the positive end).
 *
 *            Signs, as in the reference manual: CALP raises the RTC frequency
 *            and CALM lowers it. A fast RTC (positive offset against its
 *            sender) is slowed with a negative correction, i.e. CALM alone;
 *            a slow one uses CALP = 1 with CALM = 512 - N, N >= 1.
 *            The correction here is the frequency change applied to the RTC,
 *            not the offset it cancels.
 *
 *            The register also has CALW8 and CALW16 (8 s and 16 s windows);
 *            they are always 0 here: the 32 s window gives the finest step.
 *
 *            Integer arithmetic only. The 64-bit divisions are the ones
 *            rtc_set_plan.h already uses.
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
#ifndef RTC_CALR_H
#define RTC_CALR_H

#include <stdint.h>
#include <stdbool.h>

/*! Pulses in a 32 s calibration cycle: 2^20 at RTCCLK = 32 768 Hz. */
#define RTC_CALR_CYCLE_PULSES   1048576
/*! Largest number of pulses added per cycle (CALP = 1, CALM = 0). */
#define RTC_CALR_N_MAX          512
/*! Largest number of pulses masked per cycle (CALP = 0, CALM = 511). */
#define RTC_CALR_N_MIN          (-511)
/*! One step in ppb, nominal (1e9 / 2^20 = 953.67). */
#define RTC_CALR_STEP_PPB       954
/*! RTC_CALR bit positions: CALP is bit 15, CALM is bits 8:0. */
#define RTC_CALR_CALP_BIT       (1u << 15)
#define RTC_CALR_CALM_MASK      0x1FFu
#define RTC_CALR_CALW_MASK      ((1u << 14) | (1u << 13))   /* CALW8, CALW16 */

typedef struct {
    uint8_t  calp;   /*!< 0 or 1: add 512 pulses per cycle. */
    uint16_t calm;   /*!< 0..511: pulses masked per cycle. */
} RtcCalr_t;

/*! \brief  Net pulses added per 32 s cycle, N = CALP x 512 - CALM. */
static inline int32_t RtcCalr_Pulses(RtcCalr_t c)
{
    return (int32_t)c.calp * RTC_CALR_N_MAX - (int32_t)c.calm;
}

/*! \brief  The setting for N net pulses per cycle, N clamped to the range. */
static inline RtcCalr_t RtcCalr_FromPulses(int32_t n)
{
    RtcCalr_t c;
    if (n > RTC_CALR_N_MAX) n = RTC_CALR_N_MAX;
    if (n < RTC_CALR_N_MIN) n = RTC_CALR_N_MIN;
    if (n >= 1) {
        c.calp = 1u;
        c.calm = (uint16_t)(RTC_CALR_N_MAX - n);
    } else {
        c.calp = 0u;
        c.calm = (uint16_t)(-n);
    }
    return c;
}

/*!
 * \brief   The setting closest to a frequency change of \c ppb, saturated at
 *          the ends of the range (-487 090 .. +488 520 ppb).
 *
 * \details N = round(2^20 x ppb / (1e9 + ppb)), the exact inverse of the
 *          frequency formula, rounded to the nearest pulse (halves away
 *          from zero).
 */
static inline RtcCalr_t RtcCalr_FromPpb(int32_t ppb)
{
    /* Far beyond the range already: keeps the maths free of overflow and of
     * a zero denominator. */
    if (ppb > 1000000) ppb = 1000000;
    if (ppb < -1000000) ppb = -1000000;

    int64_t num = (int64_t)RTC_CALR_CYCLE_PULSES * ppb;
    int64_t den = 1000000000LL + ppb;
    int64_t half = den / 2;
    int64_t n = (num >= 0) ? (num + half) / den : -((-num + half) / den);
    return RtcCalr_FromPulses((int32_t)n);
}

/*!
 * \brief   The frequency change a setting applies, in ppb (+ faster),
 *          rounded to the nearest ppb: 1e9 x N / (2^20 - N).
 */
static inline int32_t RtcCalr_ToPpb(RtcCalr_t c)
{
    int64_t n   = RtcCalr_Pulses(c);
    int64_t num = n * 1000000000LL;
    int64_t den = (int64_t)RTC_CALR_CYCLE_PULSES - n;
    int64_t half = den / 2;
    return (int32_t)((num >= 0) ? (num + half) / den : -((-num + half) / den));
}

/*! \brief  RTC_CALR register value of a setting (32 s window). */
static inline uint32_t RtcCalr_ToReg(RtcCalr_t c)
{
    return (c.calp ? RTC_CALR_CALP_BIT : 0u) | ((uint32_t)c.calm & RTC_CALR_CALM_MASK);
}

/*!
 * \brief   The setting held by an RTC_CALR value.
 * \return  false when the register selects an 8 s or 16 s window, whose
 *          pulses-per-cycle maths differs: \c out is then not valid.
 */
static inline bool RtcCalr_FromReg(uint32_t reg, RtcCalr_t *out)
{
    if ((reg & RTC_CALR_CALW_MASK) != 0u) return false;
    out->calp = (reg & RTC_CALR_CALP_BIT) ? 1u : 0u;
    out->calm = (uint16_t)(reg & RTC_CALR_CALM_MASK);
    return true;
}

#endif /* RTC_CALR_H */
