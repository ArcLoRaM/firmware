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
 * Two-Sync-phase frame
 *
 * Two Sync Phases, each with ten cells. C3 transmits in the first
 * SYNC_TX_BUDGET cells and skips the rest, which spaces its bursts out;
 * three cells are enough for the three-packet acquisition sequence that
 * drives C1/C2 from CLOCK_COLD to CLOCK_WARM.
 * The second Sync phase exercises the multi-Sync-phase advance path:
 * after the last cell of Sync0 the cursor moves to Sync1 (same frame),
 * not to the first phase of the next frame.
 *
 * Replace with the full frame layout once all Phase Types are finalised.
 * ========================================================================= */
#ifndef SYNC_CELL_GAP_MS
/*!
 * Gap after each Sync cell, ms. The default schedule (two Sync phases of ten
 * cells, a cell every 3 s, C3 and C2 sending in the first SYNC_TX_BUDGET = 3
 * cells of each) puts 5.95 s of airtime in every 60 s frame: 9.9 % duty cycle
 * against the band's 1 %. The 36 s credit is gone after 6.7 min, then a node
 * sends one packet in ~99 s (TX_DENIED in between). That is a bring-up
 * schedule, not a product one (#45).
 *
 * A multi-hour run needs a schedule inside the budget: the Build Overrides
 * SYNC_TX_BUDGET=1u and SYNC_CELL_GAP_MS=9500u give a Sync phase every 120 s
 * with one packet per phase and node, 0.83 % duty cycle, never denied
 * (Tests/unit/test_sync_budget.c computes both with the real compliance
 * engine).
 */
#define SYNC_CELL_GAP_MS  500u
#endif

static const Phase_t s_sync_phase_0 = {
    .type             = PHASE_TYPE_SYNC,
    .participant_mask = PARTICIPANT_C1 | PARTICIPANT_C2 | PARTICIPANT_C3,
    .direction_mode   = DIRECTION_MAC_CELL,
    .cell_count       = 10u,
    .slot_count       = 1u,
    .slot_active_ms   = 2500u,    /* SF12/BW125 Sync packet airtime (conservative) */
    .gap_after_slot_ms = SYNC_CELL_GAP_MS,  /* Inter-cell guard gap */
    .header           = { .duration_ms = 0u },
    .footer           = { .duration_ms = 0u },
};

static const Phase_t s_sync_phase_1 = {
    .type             = PHASE_TYPE_SYNC,
    .participant_mask = PARTICIPANT_C1 | PARTICIPANT_C2 | PARTICIPANT_C3,
    .direction_mode   = DIRECTION_MAC_CELL,
    .cell_count       = 10u,
    .slot_count       = 1u,
    .slot_active_ms   = 2500u,    /* SF12/BW125 Sync packet airtime (conservative) */
    .gap_after_slot_ms = SYNC_CELL_GAP_MS,  /* Inter-cell guard gap */
    .header           = { .duration_ms = 0u },
    .footer           = { .duration_ms = 0u },
};

static const Phase_t * const s_table[] = {
    &s_sync_phase_0,
    &s_sync_phase_1,
};

#define TABLE_SIZE  ( (uint8_t)( sizeof(s_table) / sizeof(s_table[0]) ) )

/* Phase start offsets, computed by TdmaTable_Init at boot. */
static uint32_t s_phase_start_ms[2];

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
