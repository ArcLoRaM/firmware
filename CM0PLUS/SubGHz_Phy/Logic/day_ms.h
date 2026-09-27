/*!
 * \file      day_ms.h
 *
 * \brief     Midnight-safe arithmetic on RTC times, in ms since midnight.
 *
 * \details   The RTC reads ms since midnight (\c GetTimerTicks) and wraps at
 *            \ref MS_PER_DAY. Every time the TDMA Machine and the MAC compare
 *            or advance is kept in that day domain [0, MS_PER_DAY): a sum is
 *            reduced modulo a day, a difference is signed and taken the short
 *            way round the day, so 23:59:59.900 is 200 ms before 00:00:00.100.
 *            Valid for intervals under 12 h, far above any slot, guard or
 *            silence timeout.
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
#ifndef DAY_MS_H
#define DAY_MS_H

#include <stdint.h>

/*! Milliseconds per day: the wrap of the ms-since-midnight RTC domain. */
#define MS_PER_DAY  86400000u

/*!
 * \brief   \c t advanced by \c delta_ms (negative: moved back), in the day
 *          domain.
 *
 * \param   t         Time in ms since midnight (any value; reduced first).
 * \param   delta_ms  Signed offset, |delta_ms| < \ref MS_PER_DAY.
 */
static inline uint32_t DayMs_Add(uint32_t t, int32_t delta_ms)
{
    /* t + day + delta lies in (0, 3 days): exact in uint32_t, where adding
     * the two's-complement of a negative delta wraps to the true sum. */
    return ((t % MS_PER_DAY) + MS_PER_DAY + (uint32_t)delta_ms) % MS_PER_DAY;
}

/*!
 * \brief   Signed \c a − \c b the short way round the day, in
 *          (−12 h, +12 h]: positive when \c a is after \c b.
 */
static inline int32_t DayMs_Diff(uint32_t a, uint32_t b)
{
    uint32_t d = ((a % MS_PER_DAY) + MS_PER_DAY - (b % MS_PER_DAY)) % MS_PER_DAY;
    return (d > MS_PER_DAY / 2u) ? (int32_t)d - (int32_t)MS_PER_DAY : (int32_t)d;
}

/*! \brief  |\c a − \c b| the short way round the day. */
static inline uint32_t DayMs_AbsDiff(uint32_t a, uint32_t b)
{
    int32_t d = DayMs_Diff(a, b);
    return (d < 0) ? (uint32_t)(-d) : (uint32_t)d;
}

/*!
 * \brief   Monotonic ms counter built from time-of-day readings.
 *
 * \details For time bases that must not jump back at midnight (UTIL_TIMER
 *          context and elapsed time, \c HAL_GetTick, the compliance engine's
 *          credit refill) while the only clock running in Stop2 is the RTC,
 *          which reads ms since midnight. Each update adds the forward step
 *          since the previous reading: midnight is a short forward step. A
 *          backward step (the RTC written back) is not counted, so the
 *          counter never goes back. Updates must be less than 12 h apart; a
 *          missed step is then under-counted, never over-counted.
 *          \c mono_ms wraps at 2^32 like any tick counter.
 */
typedef struct {
    uint32_t mono_ms;      /*!< Monotonic ms. */
    uint32_t last_day_ms;  /*!< Time-of-day reading of the latest update. */
} DayMsClock_t;

/*! \brief  Start the counter at the current time of day. */
static inline void DayMsClock_Init(DayMsClock_t *c, uint32_t day_ms)
{
    c->mono_ms     = day_ms % MS_PER_DAY;
    c->last_day_ms = day_ms % MS_PER_DAY;
}

/*! \brief  Advance from a new time-of-day reading; returns the counter. */
static inline uint32_t DayMsClock_Update(DayMsClock_t *c, uint32_t day_ms)
{
    int32_t step = DayMs_Diff(day_ms, c->last_day_ms);
    if (step > 0) {
        c->mono_ms += (uint32_t)step;
    }
    c->last_day_ms = day_ms % MS_PER_DAY;
    return c->mono_ms;
}

#endif /* DAY_MS_H */
