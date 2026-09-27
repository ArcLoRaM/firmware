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
#include <stdbool.h>

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
 * \details For time bases that must not jump back at midnight or when the
 *          RTC is corrected (UTIL_TIMER context and elapsed time,
 *          \c HAL_GetTick, the compliance engine's credit refill), while the
 *          only clock running in Stop2 is the RTC, which reads ms since
 *          midnight.
 *
 *          Each update adds the step since the previous reading: midnight is
 *          a short forward step. RTC writes are excluded, so the counter
 *          follows real time through every correction:
 *          - a calendar set is synchronous: the first reading after it is
 *            taken as the continuation of the last one
 *            (\ref DayMsClock_OnCalendarSet);
 *          - a sub-second shift (SHIFTR) is applied by the hardware at once
 *            or later, while SHPF is set: its size is held as pending and
 *            removed from the step read once SHPF is clear
 *            (\ref DayMsClock_OnShift, \ref DayMsClock_Update).
 *          A backward step that is not a known write is not counted, so the
 *          counter never goes back. Updates must be less than 12 h apart.
 *          \c mono_ms wraps at 2^32 like any tick counter.
 */
typedef struct {
    uint32_t mono_ms;          /*!< Monotonic ms. */
    uint32_t last_day_ms;      /*!< Time-of-day reading of the latest update. */
    int32_t  pending_shift_ms; /*!< Shift written, not yet in the readings. */
} DayMsClock_t;

/*! \brief  Start the counter at the current time of day. */
static inline void DayMsClock_Init(DayMsClock_t *c, uint32_t day_ms)
{
    c->mono_ms          = day_ms % MS_PER_DAY;
    c->last_day_ms      = day_ms % MS_PER_DAY;
    c->pending_shift_ms = 0;
}

/*!
 * \brief   Advance from a new time-of-day reading; returns the counter.
 *
 * \param   shift_pending  SHPF at this reading: a shift written is not
 *                         applied yet. When clear, a pending shift is in
 *                         the reading and is excluded from the step.
 */
static inline uint32_t DayMsClock_Update(DayMsClock_t *c, uint32_t day_ms,
                                         bool shift_pending)
{
    if (!shift_pending && c->pending_shift_ms != 0) {
        c->last_day_ms      = DayMs_Add(c->last_day_ms, c->pending_shift_ms);
        c->pending_shift_ms = 0;
    }
    int32_t step = DayMs_Diff(day_ms, c->last_day_ms);
    if (step > 0) {
        c->mono_ms += (uint32_t)step;
    }
    c->last_day_ms = day_ms % MS_PER_DAY;
    return c->mono_ms;
}

/*!
 * \brief   The calendar was set: \c day_ms is the first reading of the new
 *          time, taken as the continuation of the last reading (the time the
 *          write took, well under 1 ms, is not counted).
 *
 * \param   shift_pending  SHPF at this reading.
 * \param   shift_ms       Shift written right after the set, 0 if none.
 *                         With SHPF clear it is already in \c day_ms; with
 *                         SHPF set it is pending (or, when none was written,
 *                         an earlier shift still is).
 */
static inline void DayMsClock_OnCalendarSet(DayMsClock_t *c, uint32_t day_ms,
                                            bool shift_pending, int32_t shift_ms)
{
    c->last_day_ms = day_ms % MS_PER_DAY;
    if (!shift_pending) {
        c->pending_shift_ms = 0;
    } else if (shift_ms != 0) {
        c->pending_shift_ms = shift_ms;
    }
}

/*!
 * \brief   A shift of \c shift_ms was written (only ever with no shift
 *          pending before it); \c day_ms is the reading right after.
 */
static inline uint32_t DayMsClock_OnShift(DayMsClock_t *c, uint32_t day_ms,
                                          bool shift_pending, int32_t shift_ms)
{
    c->pending_shift_ms = shift_ms;
    return DayMsClock_Update(c, day_ms, shift_pending);
}

#endif /* DAY_MS_H */
