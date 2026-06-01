/*!
 * \file      mac_state_machine_c3.c
 *
 * \brief     MAC State Machine — C3 (gateway / SyncAnchor) implementation.
 *
 * \details   C3 boots directly into \ref MAC_STATE_ACTIVE — it requires no
 *            three-packet Sync acquisition. It is the sole time authority
 *            for the network. BeaconTxBudget is bypassed unconditionally;
 *            phase_tx_flag is always 1 (C3 transmits every Sync phase).
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
#include "mac_state_machine.h"
#include "tdma_table.h"
#include <stddef.h>
#include <stdint.h>

/* =========================================================================
 * Module state
 * ========================================================================= */

static MacState_t            s_mac_state;
static uint8_t               s_hop_count;
static CellEligibilityMask_t s_cell_elig_ul;
static CellEligibilityMask_t s_cell_elig_dl;
static PhaseTxFlag_t         s_phase_tx_flag;
static uint8_t               s_last_phase_idx;
static MAC_Hooks_t           s_hooks;

/* =========================================================================
 * Public API
 * ========================================================================= */

void MAC_Init(const MAC_Hooks_t *hooks)
{
    s_mac_state      = MAC_STATE_ACTIVE;
    s_hop_count      = 0u;
    s_cell_elig_ul   = 0x00u;
    s_cell_elig_dl   = 0x00u;
    s_phase_tx_flag  = 1u;     /* C3 always TX on Sync phase */
    s_last_phase_idx = 0xFFu;
    if (hooks != NULL) {
        s_hooks = *hooks;
    } else {
        s_hooks.rtc_set             = NULL;
        s_hooks.rtc_align_subsecond = NULL;
        s_hooks.sync_locked         = NULL;
        s_hooks.sync_lost           = NULL;
    }
}

SlotDecision_t MAC_OnSlotOpportunity(const FrameCursor_t *cursor,
                                      const Phase_t       *phase)
{
    /* Detect phase entry */
    if (cursor->phase_index != s_last_phase_idx) {
        s_last_phase_idx = cursor->phase_index;
        if (phase->type == PHASE_TYPE_SYNC) {
            s_phase_tx_flag = 1u;   /* C3: always participation cycle */
        }
    }

    switch (phase->direction_mode) {

    case DIRECTION_MAC_PHASE:
        return (s_phase_tx_flag != 0u) ? SLOT_TX : SLOT_RX;

    case DIRECTION_MAC_CELL:
        /* C3 bypasses BeaconTxBudget — transmit unconditionally in K-range */
        return (cursor->cell_index < BEACON_K_TX_CELLS) ? SLOT_TX : SLOT_RX;

    case DIRECTION_CELL_SKIP: {
        /* C3 uses effective hop_count = 3 for uplink eligibility */
        uint8_t  eff_hop = 3u;
        uint8_t  residue = (uint8_t)(cursor->cell_index % 3u);
        uint8_t  mask    = (uint8_t)((1u << (eff_hop % 3u))
                                    | (1u << ((eff_hop + 1u) % 3u)));
        if (!((mask >> residue) & 1u)) return SLOT_SKIP;
        return (residue == (eff_hop % 3u)) ? SLOT_TX : SLOT_RX;
    }

    default:
        return SLOT_RX;
    }
}

void MAC_OnSyncPacketReceived(const SyncPayload_t *payload,
                               uint32_t             preamble_timestamp_ms)
{
    /* C3 is the SyncAnchor — it does not perform Sync acquisition */
    (void)payload;
    (void)preamble_timestamp_ms;
}

void MAC_OnBeaconReceived(const BeaconPayload_t *beacon)
{
    if (beacon == NULL) return;
    /* C3 may receive beacons from downstream C2 peers to update routing state */
    s_hop_count    = (uint8_t)(beacon->hop_count + 1u);
    s_cell_elig_ul = (uint8_t)((1u << (s_hop_count % 3u))
                              | (1u << ((s_hop_count + 1u) % 3u)));
    s_cell_elig_dl = (uint8_t)((1u << ((s_hop_count + 2u) % 3u))
                              | (1u << ((s_hop_count + 1u) % 3u)));
}

void MAC_OnExternalSyncAcquired(const FrameEpoch_t *epoch)
{
    /* Reserved for future GPS / external time source acquisition */
    (void)epoch;
}

MacState_t            MAC_GetState(void)                        { return s_mac_state;     }
ClockState_t          MAC_GetClockState(void)                   { return CLOCK_WARM;      }  /* C3 is always warm */
CellEligibilityMask_t MAC_GetCellEligibilityMask_Uplink(void)   { return s_cell_elig_ul;  }
CellEligibilityMask_t MAC_GetCellEligibilityMask_Downlink(void) { return s_cell_elig_dl;  }
PhaseTxFlag_t         MAC_GetPhaseTxFlag(void)                  { return s_phase_tx_flag; }
