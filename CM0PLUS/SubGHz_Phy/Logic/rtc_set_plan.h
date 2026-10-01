/*!
 * \file      rtc_set_plan.h
 *
 * \brief     Calendar second and SHIFTR of a Packet 1 / Tier 3 RTC set, so
 *            the new clock reads the sender's time once the write is done.
 *
 * \details   The MAC asks for target_ms at the instant of its RTC snapshot
 *            (the anchor): target_ms = target at the SyncStamp + the time
 *            since the stamp, read as floor_ms(anchor). Writing the calendar
 *            then takes time: on the CM0+ at 4 MHz the HAL path was ~2 ms,
 *            and an init-mode exit restarts the calendar at the written
 *            second's .000, so that time was lost (#55: the C2 ran 2-3 ms
 *            behind the C3). Here the loss is measured, not guessed: at any
 *            old-clock instant t the new clock must read
 *
 *                target_ms - floor_ms(anchor) + t
 *
 *            The platform reads the old clock exactly on a tick edge, enters
 *            init mode at once, writes \ref RtcSetPlan_t::seconds and restarts
 *            the calendar; the calendar stands still for loss_us from that
 *            edge. The SHIFTR then moves the clock by base_ticks + the ticks
 *            from the anchor to the edge: an advance when positive, a delay
 *            when negative. Times are RTC ticks (1/\ref RTC_TICKS_PER_S s)
 *            since midnight, computed in units of 1/4 096 000 s, exact for
 *            both ms and ticks. The ms floors of the stamp and the anchor
 *            are those the MAC already used, so they cancel.
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
#ifndef RTC_SET_PLAN_H
#define RTC_SET_PLAN_H

#include <stdint.h>

/*! RTC ticks per second: PREDIV_S + 1 (RTC_PREDIV_S = 4095). */
#define RTC_TICKS_PER_S           4096u
/*! Ticks per day: the wrap of the tick time of day. */
#define RTC_DAY_TICKS             (86400u * RTC_TICKS_PER_S)
/*! Oldest anchor a plan accepts (250 ms): keeps the SHIFTR under one second. */
#define RTC_SET_ANCHOR_MAX_TICKS  (RTC_TICKS_PER_S / 4u)

#define RTC_SET_PLAN_U_PER_MS   4096LL      /* units of 1/4 096 000 s */
#define RTC_SET_PLAN_U_PER_TICK 1000LL
#define RTC_SET_PLAN_U_PER_S    4096000LL

typedef struct {
    uint32_t seconds;     /*!< Calendar second to write, [0, 86400). */
    int32_t  base_ticks;  /*!< SHIFTR before the anchor-to-edge ticks are added. */
} RtcSetPlan_t;

/*! \brief  Floor of a tick time of day in ms: what GetTimerTicks reads. */
static inline uint32_t RtcTicks_ToMs(uint32_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000u) / RTC_TICKS_PER_S);
}

/*! \brief  Ticks from \c from to \c to, forward, across midnight. */
static inline uint32_t RtcTicks_Elapsed(uint32_t from, uint32_t to)
{
    return ((to % RTC_DAY_TICKS) + RTC_DAY_TICKS - (from % RTC_DAY_TICKS)) % RTC_DAY_TICKS;
}

/*!
 * \brief   Plan a set, before the timing-critical part (64-bit maths).
 *
 * \param   target_ms     Time the clock must read at the anchor, ms since midnight.
 * \param   anchor_ticks  Old-clock reading the target refers to (the snapshot).
 * \param   loss_us       Time the calendar stands still from the edge read to its restart.
 */
static inline RtcSetPlan_t RtcSetPlan_Make(uint32_t target_ms, uint32_t anchor_ticks,
                                           uint32_t loss_us)
{
    const int64_t day_u = 86400LL * RTC_SET_PLAN_U_PER_S;
    /* Wanted at the restart, minus the anchor-to-edge ticks added later:
     * target - floor_ms(anchor) + anchor + loss. */
    int64_t frac_u = (int64_t)anchor_ticks * RTC_SET_PLAN_U_PER_TICK
                   - (int64_t)RtcTicks_ToMs(anchor_ticks) * RTC_SET_PLAN_U_PER_MS;
    int64_t loss_u = ((int64_t)loss_us * RTC_SET_PLAN_U_PER_MS + 500) / 1000;
    int64_t want_u = ((int64_t)target_ms * RTC_SET_PLAN_U_PER_MS + frac_u + loss_u) % day_u;

    /* Nearest second: the shift stays within half a second either way. */
    int64_t s = (want_u + RTC_SET_PLAN_U_PER_S / 2) / RTC_SET_PLAN_U_PER_S;
    int64_t base_u = want_u - s * RTC_SET_PLAN_U_PER_S;

    RtcSetPlan_t p;
    p.seconds = (uint32_t)(s % 86400);
    p.base_ticks = (int32_t)((base_u >= 0 ? base_u + RTC_SET_PLAN_U_PER_TICK / 2
                                          : base_u - RTC_SET_PLAN_U_PER_TICK / 2)
                             / RTC_SET_PLAN_U_PER_TICK);
    return p;
}

/*!
 * \brief   SHIFTR ticks once the edge is read: + advance, - delay, 0 none.
 *          32-bit only, cheap enough for the timing-critical part.
 */
static inline int32_t RtcSetPlan_ShiftTicks(const RtcSetPlan_t *p, uint32_t anchor_ticks,
                                            uint32_t edge_ticks)
{
    return p->base_ticks + (int32_t)RtcTicks_Elapsed(anchor_ticks, edge_ticks);
}

#endif /* RTC_SET_PLAN_H */
