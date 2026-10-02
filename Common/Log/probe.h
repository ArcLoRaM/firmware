/*!
 * \file      probe.h
 *
 * \brief     Timing probe: how long a code path takes on the real chip,
 *            compiled in only by the Build Override BENCH_PROBE.
 *
 * \details   A probe stamps the boundaries of a path with the free-running
 *            SysTick (a 24-bit down-counter of CPU cycles) and logs one
 *            `PROBE` ArcLog line per segment AFTER the timed path, never
 *            inside it (an ARCLOG call costs time of its own):
 *
 * \code
 *   PROBE_START(p);
 *   HAL_RTC_SetTime(...);
 *   PROBE_MARK(p, "settime");
 *   HAL_RTC_SetDate(...);
 *   PROBE_MARK(p, "setdate");
 *   PROBE_LOG(p, "rtc_set");
 *
 *   260925T123456.7890 0S L #41 PROBE tag=rtc_set seg=settime us=672 hz=4000000
 * \endcode
 *
 *            Without BENCH_PROBE the three macros are empty statements: no
 *            code, no RAM, no variable, nothing in a production image. The
 *            probe lines can therefore stay in the source, where the next
 *            measurement finds them. Turn them on with
 *            `bench run ... -D BENCH_PROBE=1` (or `[overrides]` in a
 *            scenario) and expect the line: `--expect "2 PROBE tag=rtc_set"`.
 *
 *            Rules of a probe:
 *              - labels and tags are string literals without spaces (one
 *                ArcLog value each);
 *              - at most PROBE_MAX_MARKS marks per probe, extra marks are
 *                dropped;
 *              - a segment is read modulo the counter: 4.19 s at 4 MHz, so
 *                a longer span reads short; and SysTick stops in STOP2, so
 *                a path that can sleep must be timed with the RTC instead;
 *              - the probe takes over SysTick (the CM0+ HAL tick uses it):
 *                harmless because HAL_GetTick comes from the RTC, and only
 *                in a build with BENCH_PROBE.
 *
 *            Host tests supply their own clock (HOST_TEST: Probe_HostNow,
 *            Probe_HostHz).
 *
 * \author    Simon R.C. Langlais ( Celium )
 *
 */
#ifndef PROBE_H
#define PROBE_H

#ifdef BENCH_PROBE

#include <stdint.h>

#define PROBE_MAX_MARKS  8u

#if defined(HOST_TEST)
extern uint32_t Probe_HostNow(void);   /* a 24-bit down-counter, like SysTick->VAL */
extern uint32_t Probe_HostHz(void);
#  define PROBE_NOW()  Probe_HostNow()
#  define PROBE_HZ()   Probe_HostHz()
#else
#  include "stm32wlxx.h"               /* SysTick, SystemCoreClock */
#  define PROBE_NOW()  (SysTick->VAL)
#  define PROBE_HZ()   (SystemCoreClock)
#endif

typedef struct {
    uint32_t    t[PROBE_MAX_MARKS + 1u];   /* t[0] = start, t[i + 1] = mark i */
    const char *label[PROBE_MAX_MARKS];
    uint8_t     n;
} Probe_t;

/*! \brief  Make SysTick a free-running counter (once). */
void Probe_Init(void);

/*! \brief  Log one PROBE line per mark: microseconds from the previous stamp. */
void Probe_Log(const Probe_t *p, const char *tag);

static inline void Probe_Start(Probe_t *p)
{
    Probe_Init();
    p->n    = 0u;
    p->t[0] = PROBE_NOW();
}

static inline void Probe_Mark(Probe_t *p, const char *label)
{
    uint32_t now = PROBE_NOW();
    if (p->n < PROBE_MAX_MARKS) {
        p->label[p->n] = label;
        p->t[p->n + 1u] = now;
        p->n++;
    }
}

#define PROBE_START(p)        Probe_t p; Probe_Start(&(p))
#define PROBE_MARK(p, label)  Probe_Mark(&(p), (label))
#define PROBE_LOG(p, tag)     Probe_Log(&(p), (tag))

#else  /* production: nothing */

#define PROBE_START(p)        ((void)0)
#define PROBE_MARK(p, label)  ((void)0)
#define PROBE_LOG(p, tag)     ((void)0)

#endif /* BENCH_PROBE */

#endif /* PROBE_H */
