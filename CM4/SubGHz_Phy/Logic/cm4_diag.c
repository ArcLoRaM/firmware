/*!
 * \file      cm4_diag.c
 *
 * \brief     CM4 diagnostic aggregator implementation.
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
#include "cm4_diag.h"
#include <string.h>

static DiagnosticState_t s_state;

void CM4Diag_Init(void)
{
    memset(&s_state, 0, sizeof(s_state));
}

void CM4Diag_PollCompliance(const ComplianceStatus_t *status)
{
    s_state.skip_count_mesh    = status->skip_count_mesh;
    s_state.skip_count_cluster = status->skip_count_cluster;

    if (status->band_unknown_count > 0u) {
        s_state.diag_alarm_pending = true;
    }
}

const DiagnosticState_t *CM4Diag_GetState(void)
{
    return &s_state;
}
