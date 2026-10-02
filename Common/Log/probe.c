/*!
 * \file      probe.c
 *
 * \brief     Timing probe (probe.h): empty unless BENCH_PROBE is defined.
 *
 * \author    Simon R.C. Langlais ( Celium )
 */
#include "probe.h"

#ifdef BENCH_PROBE

#include <stdbool.h>
#include "arclog.h"

void Probe_Init(void)
{
#if !defined(HOST_TEST)
    static bool s_running;
    if (!s_running) {
        SysTick->LOAD = 0xFFFFFFu;
        SysTick->VAL  = 0u;
        SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;   /* no TICKINT */
        s_running = true;
    }
#endif
}

void Probe_Log(const Probe_t *p, const char *tag)
{
    uint32_t hz    = PROBE_HZ();
    uint32_t ticks_per_us = hz / 1000000u;
    if (ticks_per_us == 0u) ticks_per_us = 1u;

    for (uint8_t i = 0u; i < p->n; i++) {
        uint32_t ticks = (p->t[i] - p->t[i + 1u]) & 0xFFFFFFu;   /* the counter counts down */
        ARCLOG(ARCLOG_MOD_SYS, VLEVEL_L, "PROBE", "tag=%s seg=%s us=%u hz=%u",
               tag, p->label[i], (unsigned)(ticks / ticks_per_us), (unsigned)hz);
    }
}

#else

/* An empty translation unit is not valid C. */
typedef int probe_c_is_empty_without_bench_probe;

#endif
