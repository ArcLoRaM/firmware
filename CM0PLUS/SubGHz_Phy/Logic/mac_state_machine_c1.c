/*!
 * \file      mac_state_machine_c1.c
 *
 * \brief     MAC State Machine — C1 (end node, cluster-only) implementation.
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
static CellEligibilityMask_t s_cell_elig_ul;
static CellEligibilityMask_t s_cell_elig_dl;
static PhaseTxFlag_t         s_phase_tx_flag;
static uint8_t               s_last_phase_idx;
static MAC_Hooks_t           s_hooks;

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static uint32_t sync_per_slot_ms(void)
{
    const Phase_t *p = TdmaTable_GetPhase(0u);  /* scan for Sync phase */
    for (uint8_t i = 0u; i < TdmaTable_PhaseCount(); i++) {
        p = TdmaTable_GetPhase(i);
        if (p != NULL && p->type == PHASE_TYPE_SYNC) break;
    }
    if (p == NULL) return 0u;
    return p->slot_active_ms + p->gap_slots_ms[0];
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
    s_mac_state      = MAC_STATE_SCANNING;
    s_clock_state    = CLOCK_COLD;
    s_sync_count     = 0u;
    s_frame_epoch_ms = 0u;
    s_hop_count      = 0u;
    s_cell_elig_ul   = 0x00u;
    s_cell_elig_dl   = 0x00u;
    s_phase_tx_flag  = 0u;
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
    if (s_mac_state == MAC_STATE_SCANNING) {
        return SLOT_RX;
    }

    /* Detect phase entry */
    if (cursor->phase_index != s_last_phase_idx) {
        s_last_phase_idx = cursor->phase_index;
        /* C1 always writes phase_tx_flag = 0 */
        if (phase->type == PHASE_TYPE_SYNC) {
            s_phase_tx_flag = 0u;
        }
    }

    switch (phase->direction_mode) {

    case DIRECTION_MAC_PHASE:
        /* Sync phase: C1 is always Rx-only */
        return (s_phase_tx_flag != 0u) ? SLOT_TX : SLOT_RX;

    case DIRECTION_CELL_SKIP: {
        uint8_t residue = (uint8_t)(cursor->cell_index % 3u);
        if (!((s_cell_elig_ul >> residue) & 1u)) {
            return SLOT_SKIP;
        }
        /* TX residue = hop_count % 3; relay-RX residue = (hop_count+1) % 3 */
        return (residue == (s_hop_count % 3u)) ? SLOT_TX : SLOT_RX;
    }

    case DIRECTION_MAC_CELL:
        /* C1 does not participate in Mesh_Beacon; return RX as safe default */
        return SLOT_RX;

    default:
        return SLOT_RX;
    }
}

void MAC_OnSyncPacketReceived(const SyncPayload_t *payload,
                               uint32_t             preamble_timestamp_ms)
{
    uint32_t per_slot = sync_per_slot_ms();

    if (s_clock_state == CLOCK_COLD) {
        /* Packet 1: coarse RTC set */
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
        uint32_t expected_ms  = (uint32_t)payload->sync_slot_index * per_slot;
        uint32_t elapsed_ms   = preamble_timestamp_ms - s_frame_epoch_ms;

        if (s_sync_count == 1u) {
            /* Packet 2: sub-second alignment */
            if (s_hooks.rtc_align_subsecond != NULL) {
                s_hooks.rtc_align_subsecond(preamble_timestamp_ms, expected_ms);
            }
            s_sync_count = 2u;
            return;
        }

        if (s_sync_count == 2u) {
            /* Packet 3: lock check */
            uint32_t clock_error = u32_abs_diff(elapsed_ms, expected_ms);
            if (clock_error < SYNC_LOCK_THRESHOLD_MS) {
                s_clock_state = CLOCK_WARM;
                s_mac_state   = MAC_STATE_SYNCHRONIZED;
                s_sync_count  = 3u;
                if (s_hooks.sync_locked != NULL) s_hooks.sync_locked();
            } else {
                /* Keep trying from count=1 */
                s_sync_count = 1u;
            }
            return;
        }
    }

    if (s_clock_state == CLOCK_WARM) {
        /* Ongoing validation: check each new sync packet */
        uint32_t expected_ms  = (uint32_t)payload->sync_slot_index * per_slot;
        uint32_t elapsed_ms   = preamble_timestamp_ms - s_frame_epoch_ms;
        uint32_t clock_error  = u32_abs_diff(elapsed_ms, expected_ms);
        if (clock_error >= SYNC_LOCK_THRESHOLD_MS) {
            trigger_sync_lost();
        }
    }
}

void MAC_OnBeaconReceived(const BeaconPayload_t *beacon)
{
    if (beacon == NULL) return;

    s_hop_count    = (uint8_t)(beacon->hop_count + 1u);
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
    (void)epoch;  /* stub — GPS / external sync path; not yet implemented */
}

MacState_t            MAC_GetState(void)                        { return s_mac_state;      }
ClockState_t          MAC_GetClockState(void)                   { return s_clock_state;    }
CellEligibilityMask_t MAC_GetCellEligibilityMask_Uplink(void)   { return s_cell_elig_ul;   }
CellEligibilityMask_t MAC_GetCellEligibilityMask_Downlink(void) { return s_cell_elig_dl;   }
PhaseTxFlag_t         MAC_GetPhaseTxFlag(void)                  { return s_phase_tx_flag;  }
