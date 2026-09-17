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
    .gap_slots_ms     = { 500u }, /* Inter-cell guard gap; remaining entries = 0 */
    .header           = { .duration_ms = 0u },
    .footer           = { .duration_ms = 0u },
};

static const Phase_t * const s_table[] = {
    &s_sync_phase,
};

#define TABLE_SIZE  ( (uint8_t)( sizeof(s_table) / sizeof(s_table[0]) ) )

/* Precomputed ms offsets from Frame start, one entry per Phase.
 * Phase 0 always starts at 0 ms. */
static const uint32_t s_phase_start_ms[] = {
    0u,
};

/* =========================================================================
 * Accessor implementations
 * ========================================================================= */

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

bool TdmaTable_ValidateSyncSingleSlot_Of(const Phase_t * const *phases, uint8_t count)
{
    for (uint8_t i = 0u; i < count; i++) {
        if (phases[i] != NULL
            && phases[i]->type == PHASE_TYPE_SYNC
            && phases[i]->slot_count != 1u) {
            return false;
        }
    }
    return true;
}

bool TdmaTable_ValidateSyncSingleSlot(void)
{
    return TdmaTable_ValidateSyncSingleSlot_Of(s_table, TABLE_SIZE);
}
