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
#include "guard_time_resolver.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#ifndef SYNC_PARTICIPATE_THRESHOLD_MS
#define SYNC_PARTICIPATE_THRESHOLD_MS  8u
#endif

/* =========================================================================
 * Module state
 * ========================================================================= */

static MacState_t            s_mac_state;
static ClockState_t          s_clock_state;
static uint8_t               s_sync_consecutive;   /* consecutive good packets since P1 */
static uint32_t              s_sync_phase_ms;       /* phase start in current RTC domain */
static uint32_t              s_sync_phase_epoch_ms; /* epoch for TX relay (from C3) */
static uint8_t               s_sync_phase_day;
static uint8_t               s_sync_phase_month;
static uint8_t               s_sync_phase_year;
static bool                  s_epoch_received_this_phase;
static uint8_t               s_hop_count;
static uint16_t              s_route_cost;
static uint8_t               s_beacon_tx_budget;
static CellEligibilityMask_t s_cell_elig_ul;
static CellEligibilityMask_t s_cell_elig_dl;
static PhaseTxFlag_t         s_phase_tx_flag;
static uint8_t               s_last_phase_idx;
static uint8_t               s_first_beacon;
static MAC_Hooks_t           s_hooks;

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static uint32_t sync_per_cell_ms_for_phase(uint8_t phase_idx)
{
    const Phase_t *p = TdmaTable_GetPhase(phase_idx);
    return (p != NULL && p->type == PHASE_TYPE_SYNC)
           ? (p->slot_active_ms + p->gap_after_slot_ms) : 0u;
}

static uint32_t u32_abs_diff(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static void trigger_sync_lost(void)
{
    s_clock_state = CLOCK_COLD;
    s_mac_state   = MAC_STATE_SCANNING;
    s_sync_consecutive = 0u;
    s_epoch_received_this_phase = false;
    if (s_hooks.sync_lost != NULL) s_hooks.sync_lost();
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void MAC_Init(const MAC_Hooks_t *hooks)
{
    s_mac_state                  = MAC_STATE_SCANNING;
    s_clock_state                = CLOCK_COLD;
    s_sync_consecutive           = 0u;
    s_sync_phase_ms              = 0u;
    s_sync_phase_epoch_ms        = 0u;
    s_sync_phase_day             = 0u;
    s_sync_phase_month           = 0u;
    s_sync_phase_year            = 0u;
    s_epoch_received_this_phase  = false;
    s_hop_count                  = 0u;
    s_route_cost                 = 0u;
    s_beacon_tx_budget           = 0u;
    s_cell_elig_ul               = 0x00u;
    s_cell_elig_dl               = 0x00u;
    s_phase_tx_flag              = 0u;
    s_last_phase_idx             = 0xFFu;
    s_first_beacon               = 1u;
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

    /* Detect phase entry */
    if (cursor->phase_index != s_last_phase_idx) {
        s_last_phase_idx            = cursor->phase_index;
        s_epoch_received_this_phase = false;
    }

    switch (phase->direction_mode) {

    case DIRECTION_MAC_PHASE:
        /* Sync phase: cell 0 always RX; cells 1+ TX only if epoch received */
        if (cursor->cell_index == 0u) {
            return SLOT_RX;
        }
        return s_epoch_received_this_phase ? SLOT_TX : SLOT_RX;

    case DIRECTION_CELL_SKIP: {
        uint8_t residue = (uint8_t)(cursor->cell_index % 3u);
        if (!((s_cell_elig_ul >> residue) & 1u)) {
            return SLOT_SKIP;
        }
        return (residue == (s_hop_count % 3u)) ? SLOT_TX : SLOT_RX;
    }

    case DIRECTION_MAC_CELL: {
        if (phase->type == PHASE_TYPE_SYNC) {
            /* Sync reactive model: cell 0 always RX; cells 1+ TX only if
             * epoch received in cell 0 this occurrence. */
            if (cursor->cell_index == 0u) {
                return SLOT_RX;
            }
            return s_epoch_received_this_phase ? SLOT_TX : SLOT_RX;
        }
        /* Mesh_Beacon: Tx in the next K cells after beacon reception
         * (budget counts down from K; no absolute cell-index constraint) */
        if (s_beacon_tx_budget > 0u) {
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
    //duration of a cell in the sync phase, used to compute expected arrival time of the packet
    uint32_t per_cell = sync_per_cell_ms_for_phase(payload->sync_phase_index);

    /* ---- Packet 1: cold RTC set ----------------------------------------- */
    if (s_clock_state == CLOCK_COLD) {
        uint32_t target_ms = payload->ms_since_midnight_sync_phase
                             + (uint32_t)payload->sync_cell_index * per_cell; //the sender claimed nominal start of cell time

        //the hardware RTC calendar is now set so that,going forward, reading it should yield target_ms  (modulo the hook's own execution latency).
        if (s_hooks.rtc_set != NULL) {
            s_hooks.rtc_set(target_ms, payload->day, payload->month, payload->year);
        }

        uint32_t rtc_now = 0u;
        if (s_hooks.get_rtc_snapshot != NULL) {
            uint8_t d, mo, y;
            s_hooks.get_rtc_snapshot(&rtc_now, &d, &mo, &y);//indepedent hardware read of the RTC in the new domain
        }
        s_sync_phase_ms    = rtc_now - (uint32_t)payload->sync_cell_index * per_cell;
        // → derives "when did this Sync phase occurrenc start, in the new RTC domain" - the anchor later used to compute expected-arrival clock-error for Packets 2 and 3 of the synchronization process

        s_sync_consecutive = 0u;
        s_clock_state      = CLOCK_ACQUIRING;

        if (s_hooks.sync_bootstrapped != NULL) {
            s_hooks.sync_bootstrapped(payload->sync_phase_index,
                                       payload->sync_cell_index,
                                       rtc_now);
        }
        return;
    }

    /* ---- Packets 2+: consecutive lock check ----------------------------- */
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

    /* ---- CLOCK_WARM: three-tier per-occurrence dispatch ----------------- */
    if (s_clock_state == CLOCK_WARM) {
        uint32_t expected_arrival = payload->ms_since_midnight_sync_phase
                                    + (uint32_t)payload->sync_cell_index * per_cell;
        uint32_t error = u32_abs_diff(preamble_timestamp_ms, expected_arrival);

        if (error < SYNC_PARTICIPATE_THRESHOLD_MS) {
            /* Tier 1: participate — store epoch for cells 1+ relay */
            s_sync_phase_epoch_ms       = payload->ms_since_midnight_sync_phase;
            s_sync_phase_day            = payload->day;
            s_sync_phase_month          = payload->month;
            s_sync_phase_year           = payload->year;
            s_epoch_received_this_phase = true;

        } else if (error < SYNC_RESYNC_THRESHOLD_MS) {
            /* Tier 2: SSR-only correction; do not relay this occurrence. */
            if (s_hooks.rtc_align_subsecond != NULL) {
                s_hooks.rtc_align_subsecond(preamble_timestamp_ms, expected_arrival);
            }

        } else {
            /* Tier 3: drift ≥ MAX_GUARD_TIME_MS — full re-anchor via rtc_set */
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

    uint8_t  new_hop  = (uint8_t)(beacon->hop_count + 1u);
    uint16_t new_cost = beacon->route_cost;

    int structural_change =
        s_first_beacon ||
        (new_hop != s_hop_count) ||
        (u32_abs_diff((uint32_t)new_cost, (uint32_t)s_route_cost) > ROUTE_COST_CHANGE_THRESHOLD);

    if (structural_change) {
        s_beacon_tx_budget = BEACON_K_TX_CELLS;
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

MacState_t            MAC_GetState(void)                        { return s_mac_state;           }
ClockState_t          MAC_GetClockState(void)                   { return s_clock_state;         }
CellEligibilityMask_t MAC_GetCellEligibilityMask_Uplink(void)   { return s_cell_elig_ul;        }
CellEligibilityMask_t MAC_GetCellEligibilityMask_Downlink(void) { return s_cell_elig_dl;        }
PhaseTxFlag_t         MAC_GetPhaseTxFlag(void)                  { return s_phase_tx_flag;       }
bool                  MAC_GetEpochReceivedThisPhase(void)        { return s_epoch_received_this_phase; }
uint8_t               MAC_GetHopCount(void)                      { return s_hop_count; }
uint8_t               MAC_GetBeaconTxBudget(void)                 { return s_beacon_tx_budget; }
uint32_t              MAC_GetSyncPhaseMs(void)                  { return s_sync_phase_ms;       }
uint32_t              MAC_GetSyncPhaseEpochMs(void)             { return s_sync_phase_epoch_ms; }
void MAC_GetSyncPhaseDate(uint8_t *day, uint8_t *month, uint8_t *year)
{
    if (day)   *day   = s_sync_phase_day;
    if (month) *month = s_sync_phase_month;
    if (year)  *year  = s_sync_phase_year;
}
