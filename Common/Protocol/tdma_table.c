/*!
 * \file      tdma_table.c
 *
 * \brief     TDMA Table implementation — Sync-only stub table for initial
 *            synchronisation integration testing.
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
#include "tdma_table.h"

/* =========================================================================
 * Sync-only stub table
 *
 * One Sync Phase with three cells — sufficient for the three-packet
 * acquisition sequence that drives C1/C2 from CLOCK_COLD to CLOCK_WARM.
 *
 * Replace with the full frame layout once all Phase Types are finalised.
 * ========================================================================= */
static const Phase_t s_sync_phase = {
    .type             = PHASE_TYPE_SYNC,
    .participant_mask = PARTICIPANT_C1 | PARTICIPANT_C2 | PARTICIPANT_C3,
    .direction_mode   = DIRECTION_MAC_CELL,
    .cell_count       = 3u,
    .slot_count       = 1u,
    .slot_active_ms   = 2500u,    /* SF12/BW125 Sync packet airtime (conservative) */
    .gap_after_slot_ms = 500u,  /* Inter-cell guard gap */
    .header           = { .duration_ms = 0u },
    .footer           = { .duration_ms = 0u },
};

static const Phase_t * const s_table[] = {
    &s_sync_phase,
};

#define TABLE_SIZE  ( (uint8_t)( sizeof(s_table) / sizeof(s_table[0]) ) )

/* Phase start offsets, computed by TdmaTable_Init at boot. */
static uint32_t s_phase_start_ms[1];

/* =========================================================================
 * Accessor implementations
 * ========================================================================= */

void TdmaTable_Init( void )
{
    s_phase_start_ms[0] = 0u;
    for ( uint8_t i = 1u; i < TABLE_SIZE; i++ ) {
        s_phase_start_ms[i] = s_phase_start_ms[i - 1u]
            + TdmaTable_PhaseDuration_ms( s_table[i - 1u] );
    }
}

const Phase_t *TdmaTable_GetPhase( uint8_t phase_index )
{
    if ( phase_index >= TABLE_SIZE ) {
        return NULL;
    }
    return s_table[phase_index];
}

uint8_t TdmaTable_PhaseCount( void )
{
    return TABLE_SIZE;
}

uint32_t TdmaTable_PhaseStartOffset_ms( uint8_t phase_index )
{
    if ( phase_index >= TABLE_SIZE ) {
        return 0u;
    }
    return s_phase_start_ms[phase_index];
}
