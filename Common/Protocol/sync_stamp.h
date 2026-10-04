/*!
 * \file      sync_stamp.h
 *
 * \brief     The SyncStamp in RTC ticks, and the Sync error in microseconds
 *            (issue #82).
 *
 * \details   The RTC counts 1/4096 s (244.14 us). The SyncStamp used to be read
 *            in whole ms, which truncates by up to 1 ms and made the error of a
 *            Sync packet noisy: sigma 0.36 ms on the NUCLEO bench (#34). Here
 *            the stamp stays in ticks, in the day domain [0, RTC_DAY_TICKS)
 *            (86 400 x 4096 = 353 894 400, under 2^32), and the error against
 *            the expected arrival, a whole schedule ms, is taken in units of
 *            1/4 096 000 s, exact for both (1 tick = 1000 u, 1 ms = 4096 u),
 *            then given in microseconds. The arithmetic is that of
 *            rtc_set_plan.h.
 *
 *            Everything is signed the short way round the day (valid under
 *            12 h), like day_ms.h.
 *
 * \author    Simon R.C. Langlais ( Celium )
 *
 */
#ifndef SYNC_STAMP_H
#define SYNC_STAMP_H

#include <stdint.h>
#include "rtc_set_plan.h"      /* RTC_DAY_TICKS, RTC_TICKS_PER_S */

#define SYNC_STAMP_U_PER_MS    4096LL
#define SYNC_STAMP_U_PER_TICK  1000LL
#define SYNC_STAMP_DAY_U       (86400LL * 4096000LL)

/*! \brief  Microseconds to the nearest RTC tick. */
static inline uint32_t SyncStamp_UsToTicks(uint32_t us)
{
    return (uint32_t)(((uint64_t)us * RTC_TICKS_PER_S + 500000u) / 1000000u);
}

/*!
 * \brief  A ms of the day as the first RTC tick not before it (reduced to the
 *         day). The floor ms of that tick is the ms itself, so a whole-ms stamp
 *         round-trips: the old millisecond interface keeps its ms, and reads
 *         0 to 244 us late.
 */
static inline uint32_t SyncStamp_MsToTicks(uint32_t ms)
{
    return (uint32_t)((((uint64_t)ms * RTC_TICKS_PER_S + 999u) / 1000u) % RTC_DAY_TICKS);
}

/*!
 * \brief   The packet start on air, in day ticks: the RTC at the RxDone
 *          interrupt entry minus the time on air minus the Rx latency, back
 *          across midnight when the packet started before it.
 */
static inline uint32_t SyncStamp_FromRxDone(uint32_t rxd_ticks, uint32_t toa_us, uint32_t latency_us)
{
    uint64_t back = (uint64_t)SyncStamp_UsToTicks(toa_us) + SyncStamp_UsToTicks(latency_us);
    back %= RTC_DAY_TICKS;
    return (uint32_t)(((uint64_t)(rxd_ticks % RTC_DAY_TICKS) + RTC_DAY_TICKS - back) % RTC_DAY_TICKS);
}

/*!
 * \brief   The same stamp with the time on air and the Rx latency already in
 *          ticks (both under a day): 32-bit adds only, for the radio
 *          interrupt, where the 64-bit form cost 550 us on the CM0+ at 4 MHz
 *          (bench, 2026-10-04) against 133 us for the driver's whole-ms ToA.
 *          The modem configuration does not change, so the caller converts
 *          the air time once.
 *
 * \param   rxd_ticks   RTC at the RxDone interrupt entry, day ticks.
 * \param   back_ticks  Time on air plus Rx latency, in ticks.
 */
static inline uint32_t SyncStamp_FromRxDoneTicks(uint32_t rxd_ticks, uint32_t back_ticks)
{
    return (rxd_ticks >= back_ticks) ? (rxd_ticks - back_ticks)
                                     : (rxd_ticks + RTC_DAY_TICKS - back_ticks);
}

/*!
 * \brief   Stamp minus expected arrival, in us (positive: the stamp is late,
 *          the local clock is ahead), the short way round the day.
 *
 * \param   stamp_ticks   The SyncStamp, day ticks.
 * \param   expected_ms   The expected arrival, ms since midnight, from the
 *                        schedule: exact, not rounded to a tick.
 */
static inline int32_t SyncStamp_ErrorUs(uint32_t stamp_ticks, uint32_t expected_ms)
{
    int64_t d = (int64_t)(stamp_ticks % RTC_DAY_TICKS) * SYNC_STAMP_U_PER_TICK
              - (int64_t)(expected_ms % 86400000u) * SYNC_STAMP_U_PER_MS;
    d = ((d % SYNC_STAMP_DAY_U) + SYNC_STAMP_DAY_U) % SYNC_STAMP_DAY_U;
    if (d > SYNC_STAMP_DAY_U / 2) d -= SYNC_STAMP_DAY_U;
    /* u -> us: x 1e6 / 4 096 000 = x 125 / 512, to the nearest */
    int64_t n = d * 125;
    int64_t r = (n >= 0) ? (n + 256) / 512 : -((-n + 256) / 512);
    return (int32_t)r;
}

/*! \brief  An error in us to the nearest ms, halves away from zero. */
static inline int32_t SyncStamp_UsToMs(int32_t us)
{
    return (us >= 0) ? (us + 500) / 1000 : -((-us + 500) / 1000);
}

#endif /* SYNC_STAMP_H */
