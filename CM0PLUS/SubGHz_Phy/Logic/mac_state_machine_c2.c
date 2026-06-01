/*!
 * \file      mac_state_machine_c2.c
 *
 * \brief     MAC State Machine — C2 (relay, mesh + cluster) implementation.
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
static ClockState_t          s_clock_state;
static uint8_t               s_sync_count;
static uint32_t              s_frame_epoch_ms;
static uint8_t               s_hop_count;
static uint16_t              s_route_cost;
static uint8_t               s_beacon_tx_budget;
static uint8_t               s_audit_cycle_counter;
static CellEligibilityMask_t s_cell_elig_ul;
static CellEligibilityMask_t s_cell_elig_dl;
static PhaseTxFlag_t         s_phase_tx_flag;
static uint8_t               s_last_phase_idx;
static uint8_t               s_first_beacon;
static MAC_Hooks_t           s_hooks;

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static uint32_t sync_per_slot_ms(void)
{
    for (uint8_t i = 0u; i < TdmaTable_PhaseCount(); i++) {
        const Phase_t *p = TdmaTable_GetPhase(i);
        if (p != NULL && p->type == PHASE_TYPE_SYNC) {
            return p->slot_active_ms + p->gap_slots_ms[0];
        }
    }
    return 0u;
}

static uint32_t u32_abs_diff(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static void trigger_sync_lost(void)
{
    s_clock_state = CLOCK_COLD;
    s_mac_state   = MAC_STATE_SCANNING;
    s_sync_count  = 0u;
    if (s_hooks.sync_lost != NULL) s_hooks.sync_lost();
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void MAC_Init(const MAC_Hooks_t *hooks)
{
    s_mac_state           = MAC_STATE_SCANNING;
    s_clock_state         = CLOCK_COLD;
    s_sync_count          = 0u;
    s_frame_epoch_ms      = 0u;
    s_hop_count           = 0u;
    s_route_cost          = 0u;
    s_beacon_tx_budget    = 0u;
    s_audit_cycle_counter = 0u;
    s_cell_elig_ul        = 0x00u;
    s_cell_elig_dl        = 0x00u;
    s_phase_tx_flag       = 0u;
    s_last_phase_idx      = 0xFFu;
    s_first_beacon        = 1u;
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
    if (s_mac_state == MAC_STATE_SCANNING) {
        return SLOT_RX;
    }

    /* Detect phase entry */
    if (cursor->phase_index != s_last_phase_idx) {
        s_last_phase_idx = cursor->phase_index;
        if (phase->type == PHASE_TYPE_SYNC) {
            /* C2 alternates: even cycle = participation (TX=1), odd = audit (TX=0) */
            s_phase_tx_flag = (s_audit_cycle_counter % 2u == 0u) ? 1u : 0u;
            s_audit_cycle_counter++;
        }
    }

    switch (phase->direction_mode) {

    case DIRECTION_MAC_PHASE:
        return (s_phase_tx_flag != 0u) ? SLOT_TX : SLOT_RX;

    case DIRECTION_CELL_SKIP: {
        uint8_t residue = (uint8_t)(cursor->cell_index % 3u);
        if (!((s_cell_elig_ul >> residue) & 1u)) {
            return SLOT_SKIP;
        }
        return (residue == (s_hop_count % 3u)) ? SLOT_TX : SLOT_RX;
    }

    case DIRECTION_MAC_CELL: {
        /* BeaconTxBudget governs TX eligibility; K = BEACON_K_TX_CELLS */
        if (cursor->cell_index < BEACON_K_TX_CELLS && s_beacon_tx_budget > 0u) {
            s_beacon_tx_budget--;
            return SLOT_TX;
        }
        return SLOT_RX;
    }

    default:
        return SLOT_RX;
    }
}

void MAC_OnSyncPacketReceived(const SyncPayload_t *payload,
                               uint32_t             preamble_timestamp_ms)
{
    uint32_t per_slot = sync_per_slot_ms();

    if (s_clock_state == CLOCK_COLD) {
        if (s_hooks.rtc_set != NULL) {
            s_hooks.rtc_set(payload->hours, payload->minutes, payload->seconds,
                            payload->day, payload->month, payload->year,
                            payload->subseconds);
        }
        s_frame_epoch_ms = preamble_timestamp_ms;
        s_sync_count     = 1u;
        s_clock_state    = CLOCK_ACQUIRING;
        return;
    }

    if (s_clock_state == CLOCK_ACQUIRING) {
        uint32_t expected_ms = (uint32_t)payload->sync_slot_index * per_slot;
        uint32_t elapsed_ms  = preamble_timestamp_ms - s_frame_epoch_ms;

        if (s_sync_count == 1u) {
            if (s_hooks.rtc_align_subsecond != NULL) {
                s_hooks.rtc_align_subsecond(preamble_timestamp_ms, expected_ms);
            }
            s_sync_count = 2u;
            return;
        }

        if (s_sync_count == 2u) {
            uint32_t clock_error = u32_abs_diff(elapsed_ms, expected_ms);
            if (clock_error < SYNC_LOCK_THRESHOLD_MS) {
                s_clock_state = CLOCK_WARM;
                s_mac_state   = MAC_STATE_SYNCHRONIZED;
                s_sync_count  = 3u;
                if (s_hooks.sync_locked != NULL) s_hooks.sync_locked();
            } else {
                s_sync_count = 1u;
            }
            return;
        }
    }

    if (s_clock_state == CLOCK_WARM) {
        uint32_t expected_ms = (uint32_t)payload->sync_slot_index * per_slot;
        uint32_t elapsed_ms  = preamble_timestamp_ms - s_frame_epoch_ms;
        if (u32_abs_diff(elapsed_ms, expected_ms) >= SYNC_LOCK_THRESHOLD_MS) {
            trigger_sync_lost();
        }
    }
}

void MAC_OnBeaconReceived(const BeaconPayload_t *beacon)
{
    if (beacon == NULL) return;

    uint8_t  new_hop  = (uint8_t)(beacon->hop_count + 1u);
    uint16_t new_cost = beacon->route_cost;

    /* BeaconTxBudget trigger: first beacon, hop changed, or large cost delta */
    int structural_change =
        s_first_beacon ||
        (new_hop != s_hop_count) ||
        (u32_abs_diff((uint32_t)new_cost, (uint32_t)s_route_cost) > ROUTE_COST_CHANGE_THRESHOLD);

    if (structural_change) {
        s_beacon_tx_budget = 2u;
        s_first_beacon     = 0u;
    }

    s_hop_count  = new_hop;
    s_route_cost = new_cost;

    s_cell_elig_ul = (uint8_t)((1u << (s_hop_count % 3u))
                              | (1u << ((s_hop_count + 1u) % 3u)));
    s_cell_elig_dl = (uint8_t)((1u << ((s_hop_count + 2u) % 3u))
                              | (1u << ((s_hop_count + 1u) % 3u)));

    if (s_mac_state == MAC_STATE_SYNCHRONIZED) {
        s_mac_state = MAC_STATE_PAIRED;
    }
}

void MAC_OnExternalSyncAcquired(const FrameEpoch_t *epoch)
{
    (void)epoch;
}

MacState_t            MAC_GetState(void)                        { return s_mac_state;      }
ClockState_t          MAC_GetClockState(void)                   { return s_clock_state;    }
CellEligibilityMask_t MAC_GetCellEligibilityMask_Uplink(void)   { return s_cell_elig_ul;   }
CellEligibilityMask_t MAC_GetCellEligibilityMask_Downlink(void) { return s_cell_elig_dl;   }
PhaseTxFlag_t         MAC_GetPhaseTxFlag(void)                  { return s_phase_tx_flag;  }
