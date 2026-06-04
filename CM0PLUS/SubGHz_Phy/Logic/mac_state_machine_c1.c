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
 * Constants
 * ========================================================================= */

#ifndef SYNC_PARTICIPATE_THRESHOLD_MS
#define SYNC_PARTICIPATE_THRESHOLD_MS  8u
#endif

#ifndef SYNC_RESYNC_THRESHOLD_MS
#define SYNC_RESYNC_THRESHOLD_MS  300u
#endif

/* =========================================================================
 * Module state
 * ========================================================================= */

static MacState_t            s_mac_state;
static ClockState_t          s_clock_state;
static uint8_t               s_sync_consecutive;
static uint32_t              s_sync_phase_ms;
static uint8_t               s_hop_count;
static CellEligibilityMask_t s_cell_elig_ul;
static CellEligibilityMask_t s_cell_elig_dl;
static PhaseTxFlag_t         s_phase_tx_flag;
static uint8_t               s_last_phase_idx;
static MAC_Hooks_t           s_hooks;

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static uint32_t sync_per_cell_ms_for_phase(uint8_t phase_idx)
{
    const Phase_t *p = TdmaTable_GetPhase(phase_idx);
    return (p != NULL && p->type == PHASE_TYPE_SYNC)
           ? (p->slot_active_ms + p->gap_slots_ms[0]) : 0u;
}

static uint32_t u32_abs_diff(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static void trigger_sync_lost(void)
{
    s_clock_state      = CLOCK_COLD;
    s_mac_state        = MAC_STATE_SCANNING;
    s_sync_consecutive = 0u;
    if (s_hooks.sync_lost != NULL) s_hooks.sync_lost();
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void MAC_Init(const MAC_Hooks_t *hooks)
{
    s_mac_state        = MAC_STATE_SCANNING;
    s_clock_state      = CLOCK_COLD;
    s_sync_consecutive = 0u;
    s_sync_phase_ms    = 0u;
    s_hop_count        = 0u;
    s_cell_elig_ul     = 0x00u;
    s_cell_elig_dl     = 0x00u;
    s_phase_tx_flag    = 0u;
    s_last_phase_idx   = 0xFFu;
    if (hooks != NULL) {
        s_hooks = *hooks;
    } else {
        s_hooks.rtc_set           = NULL;
        s_hooks.get_rtc_snapshot  = NULL;
        s_hooks.sync_bootstrapped = NULL;
        s_hooks.sync_locked       = NULL;
        s_hooks.sync_lost         = NULL;
    }
}

SlotDecision_t MAC_OnSlotOpportunity(const FrameCursor_t *cursor,
                                      const Phase_t       *phase)
{
    if (s_mac_state == MAC_STATE_SCANNING) {
        return SLOT_RX;
    }

    if (cursor->phase_index != s_last_phase_idx) {
        s_last_phase_idx = cursor->phase_index;
        if (phase->type == PHASE_TYPE_SYNC) {
            s_phase_tx_flag = 0u;  /* C1 always RX in sync */
        }
    }

    switch (phase->direction_mode) {

    case DIRECTION_MAC_PHASE:
        return SLOT_RX;  /* C1 is always Rx-only in Sync */

    case DIRECTION_CELL_SKIP: {
        uint8_t residue = (uint8_t)(cursor->cell_index % 3u);
        if (!((s_cell_elig_ul >> residue) & 1u)) {
            return SLOT_SKIP;
        }
        return (residue == (s_hop_count % 3u)) ? SLOT_TX : SLOT_RX;
    }

    case DIRECTION_MAC_CELL:
        return SLOT_RX;

    default:
        return SLOT_RX;
    }
}

void MAC_OnSyncPacketReceived(const SyncPayload_t *payload,
                               uint32_t             preamble_timestamp_ms)
{
    uint32_t per_cell = sync_per_cell_ms_for_phase(payload->sync_phase_index);

    if (s_clock_state == CLOCK_COLD) {
        uint32_t target_ms = payload->ms_since_midnight_sync_phase
                             + (uint32_t)payload->sync_cell_index * per_cell;

        if (s_hooks.rtc_set != NULL) {
            s_hooks.rtc_set(target_ms, payload->day, payload->month, payload->year);
        }

        uint32_t rtc_now = 0u;
        if (s_hooks.get_rtc_snapshot != NULL) {
            uint8_t d, mo, y;
            s_hooks.get_rtc_snapshot(&rtc_now, &d, &mo, &y);
        }
        s_sync_phase_ms    = rtc_now - (uint32_t)payload->sync_cell_index * per_cell;
        s_sync_consecutive = 0u;
        s_clock_state      = CLOCK_ACQUIRING;

        if (s_hooks.sync_bootstrapped != NULL) {
            s_hooks.sync_bootstrapped(payload->sync_phase_index,
                                       payload->sync_cell_index,
                                       rtc_now);
        }
        return;
    }

    if (s_clock_state == CLOCK_ACQUIRING) {
        uint32_t expected_ms = (uint32_t)payload->sync_cell_index * per_cell;
        uint32_t elapsed_ms  = preamble_timestamp_ms - s_sync_phase_ms;
        uint32_t clock_error = u32_abs_diff(elapsed_ms, expected_ms);

        if (clock_error < SYNC_PARTICIPATE_THRESHOLD_MS) {
            s_sync_consecutive++;
            if (s_sync_consecutive >= 2u) {
                s_clock_state = CLOCK_WARM;
                s_mac_state   = MAC_STATE_SYNCHRONIZED;
                if (s_hooks.sync_locked != NULL) s_hooks.sync_locked();
            }
        } else {
            s_sync_consecutive = 0u;
        }
        return;
    }

    if (s_clock_state == CLOCK_WARM) {
        uint32_t expected_arrival = payload->ms_since_midnight_sync_phase
                                    + (uint32_t)payload->sync_cell_index * per_cell;
        uint32_t error = u32_abs_diff(preamble_timestamp_ms, expected_arrival);

        if (error < SYNC_PARTICIPATE_THRESHOLD_MS) {
            /* C1 receives; no relay action needed */
        } else if (error < SYNC_RESYNC_THRESHOLD_MS) {
            /* Tier 2: SSR correction. */
            if (s_hooks.rtc_align_subsecond != NULL) {
                s_hooks.rtc_align_subsecond(preamble_timestamp_ms, expected_arrival);
            }
        } else {
            uint32_t target_ms = payload->ms_since_midnight_sync_phase
                                 + (uint32_t)payload->sync_cell_index * per_cell;
            if (s_hooks.rtc_set != NULL) {
                s_hooks.rtc_set(target_ms, payload->day, payload->month, payload->year);
            }
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
    (void)epoch;
}

MacState_t            MAC_GetState(void)                        { return s_mac_state;    }
ClockState_t          MAC_GetClockState(void)                   { return s_clock_state;  }
CellEligibilityMask_t MAC_GetCellEligibilityMask_Uplink(void)   { return s_cell_elig_ul; }
CellEligibilityMask_t MAC_GetCellEligibilityMask_Downlink(void) { return s_cell_elig_dl; }
PhaseTxFlag_t         MAC_GetPhaseTxFlag(void)                  { return s_phase_tx_flag; }
uint32_t              MAC_GetSyncPhaseMs(void)                  { return s_sync_phase_ms; }
uint32_t              MAC_GetSyncPhaseEpochMs(void)             { return 0u; }  /* C1 never relays */
void MAC_GetSyncPhaseDate(uint8_t *day, uint8_t *month, uint8_t *year)
{
    if (day)   *day   = 0u;
    if (month) *month = 0u;
    if (year)  *year  = 0u;
}
