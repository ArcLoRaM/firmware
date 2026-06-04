/*!
 * \file      freq_resolver.c
 *
 * \brief     Frequency Resolver implementation.
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
#include "freq_resolver.h"
#include <assert.h>

static const FrequencyResolverState_t *s_state = NULL;

void FrequencyResolver_Init(const FrequencyResolverState_t *state)
{
    s_state = state;
}

uint32_t FrequencyResolver_GetFreq(const FrameCursor_t *cursor, SlotPosition_t pos)
{
    if (s_state == NULL) {
        return 0u;
    }

    const PhaseFrequency_t *pf = &s_state->phases[cursor->phase_index];

    switch (pos) {
    case SLOT_POS_HEADER:
        return pf->header_freq_hz;

    case SLOT_POS_FOOTER:
        return pf->footer_freq_hz;

    case SLOT_POS_CELL:
        switch (pf->cell_mode) {
        case CELL_FREQ_STATIC: {
            uint32_t freq = pf->cell.static_freq_hz;
            if (freq == 0u) {
#if !defined(NDEBUG) && !defined(HOST_TEST)
                assert(0); /* CM4 has not populated g_freq_resolver_state yet */
#endif
                return FREQ_FALLBACK_HZ;
            }
            return freq;
        }

        case CELL_FREQ_HOP:
            return 0u;  /* deferred */

        case CELL_FREQ_OVERRIDE:
            return pf->cell.override[cursor->cell_index];

        default:
            return 0u;
        }

    default:
        return 0u;
    }
}
