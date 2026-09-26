/*!
 * \file      tdma_machine.c
 *
 * \brief     TDMA Machine implementation.
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
#include "tdma_machine.h"
#include "tdma_table.h"
#include "freq_resolver.h"
#include "compliance_engine.h"
#include "mac_state_machine.h"
#include "mac_types.h"
#include "arclog.h"
#include <stddef.h>
#include <string.h>

/* =========================================================================
 * NODE_CLASS → participant bit
 * ========================================================================= */

#ifndef NODE_CLASS
#  error "NODE_CLASS must be defined at build level (-DNODE_CLASS=NODE_CLASS_CX)"
#endif

#if   NODE_CLASS == NODE_CLASS_C1
#  define NODE_CLASS_BIT  PARTICIPANT_C1
#elif NODE_CLASS == NODE_CLASS_C2
#  define NODE_CLASS_BIT  PARTICIPANT_C2
#else
#  define NODE_CLASS_BIT  PARTICIPANT_C3
#endif

/* =========================================================================
 * Module state
 * ========================================================================= */

static TdmaPlatform_t  s_platform;
static FrameCursor_t   s_cursor;
static uint8_t         s_slot_idx;      /* slot within the current cell */
static uint32_t        s_slot_start_ms; /* nominal start of the current slot */
static uint32_t        s_expected_wake_ms;
static bool            s_cursor_suspect;
static bool            s_bootstrapped_in_slot; /* BootstrapFromSync called during this SlotTask */
/* Alarm chain armed. False while scanning (CLOCK_COLD): the radio listens
 * continuously and no slot runs. Written from the radio ISR path
 * (BootstrapFromSync, OnRxEnd) and read by SlotTask. */
static volatile bool   s_running;

/* =========================================================================
 * Internal helpers — cursor advancement
 * ========================================================================= */

static void enter_phase(uint8_t phase_idx)
{
    const Phase_t *p = TdmaTable_GetPhase(phase_idx);
    s_cursor.phase_index = phase_idx;
    s_cursor.cell_index  = 0u;
    s_cursor.slot_index  = 0u;
    s_slot_idx           = 0u;
    s_cursor.slot_pos = (p != NULL && p->header.duration_ms > 0u)
                 ? SLOT_POS_HEADER : SLOT_POS_CELL;
}

static void advance_to_next_phase(void)
{
    uint8_t next = (uint8_t)(s_cursor.phase_index + 1u);
    if (next >= TdmaTable_PhaseCount()) {
        /* Frame wrap */
        s_cursor.phase_index = 0u;
        s_cursor.cell_index  = 0u;
        s_cursor.slot_index  = 0u;
        s_slot_idx           = 0u;
        const Phase_t *p0 = TdmaTable_GetPhase(0u);
        s_cursor.slot_pos = (p0 != NULL && p0->header.duration_ms > 0u)
                     ? SLOT_POS_HEADER : SLOT_POS_CELL;
    } else {
        enter_phase(next);
    }
}

static void advance_cursor(const Phase_t *phase)
{
    switch (s_cursor.slot_pos) {
    case SLOT_POS_HEADER:
        s_cursor.slot_pos = SLOT_POS_CELL;
        s_cursor.cell_index = 0u;
        s_cursor.slot_index = 0u;
        s_slot_idx          = 0u;
        break;

    case SLOT_POS_CELL:
        if (s_slot_idx + 1u < phase->slot_count) {
            /* Next slot within the same cell */
            s_slot_idx++;
            s_cursor.slot_index = s_slot_idx;
        } else if (s_cursor.cell_index + 1u < phase->cell_count) {
            /* Next cell */
            s_cursor.cell_index++;
            s_slot_idx          = 0u;
            s_cursor.slot_index = 0u;
        } else {
            /* Last cell, last slot — move to footer or next phase */
            if (phase->footer.duration_ms > 0u) {
                s_cursor.slot_pos = SLOT_POS_FOOTER;
            } else {
                advance_to_next_phase();
            }
        }
        break;

    case SLOT_POS_FOOTER:
        advance_to_next_phase();
        break;

    default:
        /* Unreachable under correct operation — s_cursor.slot_pos only ever holds a
         * SlotPosition_t value written by enter_phase/advance_to_next_phase/
         * TdmaMachine_BootstrapFromSync. Getting here means the static has
         * been corrupted (bit-flip, stray write). A field node must not
         * crash on this, so flag it via the existing cursor-suspect signal
         * (drives MAC re-acquisition on the next checkpoint, see
         * TdmaMachine_SlotTask Step 2) and fall back to a safe phase
         * boundary instead of looping forever on garbage state. */
        s_cursor_suspect = true;
        ARCLOG(ARCLOG_MOD_TDMA, VLEVEL_L, "CURSOR_CORRUPT", "pos=%u",
               (unsigned)s_cursor.slot_pos);
        advance_to_next_phase();
        break;
    }
}

/* Skip phases that exclude the local node class; returns true if a
   participating phase was found, false if the entire frame was skipped. */
static bool skip_to_participating_phase(void)
{
    uint8_t count = TdmaTable_PhaseCount();
    for (uint8_t i = 0u; i < count; i++) {
        uint8_t    next_idx  = (uint8_t)(s_cursor.phase_index + 1u);
        if (next_idx >= count) next_idx = 0u;
        enter_phase(next_idx);

        const Phase_t *p = TdmaTable_GetPhase(s_cursor.phase_index);
        if (p != NULL && (p->participant_mask & NODE_CLASS_BIT)) {
            return true;
        }
    }
    /* All phases excluded — wrap to {0,0,0} as fallback */
    s_cursor.phase_index = 0u;
    s_cursor.cell_index  = 0u;
    s_cursor.slot_index  = 0u;
    s_slot_idx           = 0u;
    s_cursor.slot_pos      = SLOT_POS_CELL;
    return false;
}

/* =========================================================================
 * Internal helpers — timing
 * ========================================================================= */

static uint32_t u32_abs_diff(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

/* =========================================================================
 * Internal helpers - radio
 * ========================================================================= */

/* Stop the alarm chain (if running) and listen continuously on the
 * discovery channel. The next Sync packet restarts the chain through
 * TdmaMachine_BootstrapFromSync. */
static void enter_scanning(const char *why)
{
    if (s_running) {
        s_running = false;
        s_platform.CancelAlarmA();
    }
    ARCLOG(ARCLOG_MOD_TDMA, VLEVEL_M, "SCAN", "freq=%u why=%s",
           (unsigned)SCAN_FREQ_HZ, why);
    s_platform.RadioSetChannel(SCAN_FREQ_HZ);
    s_platform.RadioScan();
}

/* Open the Rx window of the current slot, from now (early wake, guard
 * applied) until the latest instant a packet can start and still end by
 * slot end + MAX_GUARD_TIME_MS. Gaps are >= 2 x MAX_GUARD_TIME_MS, so a
 * packet ending by then never reaches the next slot, whatever guard its
 * receivers apply. The expected packet is the Sync packet in every slot for
 * now (issue #38). The platform aborts any reception still running at that
 * end (cap), e.g. after a false preamble detection. */
static void open_rx_window(const Phase_t *phase, uint32_t now_ms)
{
    uint32_t cap_ms  = s_slot_start_ms + phase->slot_active_ms + MAX_GUARD_TIME_MS;
    uint32_t toa_ms  = s_platform.RadioTimeOnAir((uint8_t)sizeof(SyncPayload_t));
    uint32_t last_ms = cap_ms - toa_ms;

    if ((int32_t)(last_ms - now_ms) <= 0) {
        /* Woke too late for any packet to fit (or ToA exceeds the slot). */
        ARCLOG(ARCLOG_MOD_TDMA, VLEVEL_L, "RX_LATE", "now=%u last=%u",
               (unsigned)now_ms, (unsigned)last_ms);
        s_platform.RadioSleep();
        return;
    }
    ARCLOG(ARCLOG_MOD_TDMA, VLEVEL_H, "RX_WIN", "last=%u cap=%u",
           (unsigned)last_ms, (unsigned)cap_ms);
    s_platform.RadioSetRx(last_ms - now_ms, cap_ms - now_ms);
}

static bool next_slot_is_rx(const Phase_t *phase);

/* Wake time for the slot at s_slot_start_ms (the cursor already points at
 * it): guard-time look-ahead, early by the guard if the slot will be Rx. */
static uint32_t next_wake_ms(void)
{
    uint32_t wake_ms = s_slot_start_ms;
    const Phase_t *next_phase = TdmaTable_GetPhase(s_cursor.phase_index);
    if (next_phase != NULL && next_slot_is_rx(next_phase)) {
        wake_ms -= GuardTimeResolver_GetGuardMs();
    }
    return wake_ms;
}

/* Determine whether the next slot opportunity will be RX (for guard-time
   look-ahead).  Guard is applied when the next slot is deterministically Rx
   or when its role is uncertain (conservative Rx default).  Guard is withheld
   only when the next slot is deterministically Tx. */
static bool next_slot_is_rx(const Phase_t *phase)
{
    switch (phase->direction_mode) {

    case DIRECTION_MAC_PHASE:
        return (MAC_GetPhaseTxFlag() == 0u);

    case DIRECTION_MAC_CELL:
        if (phase->type == PHASE_TYPE_SYNC) {
#if   NODE_CLASS == NODE_CLASS_C1
            return true;               /* C1 always Rx in Sync */
#elif NODE_CLASS == NODE_CLASS_C3
            return false;              /* C3: first SYNC_TX_BUDGET cells Tx, rest Skip — not Rx */
#else  /* C2 */
            if (s_cursor.cell_index == 0u) {
                return true;           /* cell 0 always Rx */
            }
            return !(MAC_GetEpochReceivedThisPhase() && MAC_GetSyncTxBudget() > 0u);
#endif
        }
        /* Mesh_Beacon: prediction depends on node class */
#if   NODE_CLASS == NODE_CLASS_C3
        return false;              /* C3 always Tx in beacon → no guard */
#elif NODE_CLASS == NODE_CLASS_C1
        return true;               /* C1 always Rx in beacon → guard */
#else  /* C2 */
        return (MAC_GetBeaconTxBudget() == 0u);  /* budget > 0 → Tx → no guard */
#endif

    case DIRECTION_CELL_SKIP: {
        uint8_t cell_idx = (uint8_t)s_cursor.cell_index;
        CellEligibilityMask_t mask;
        if (phase->type == PHASE_TYPE_MESH_DOWNLINK) {
            mask = MAC_GetCellEligibilityMask_Downlink();
        } else {
            mask = MAC_GetCellEligibilityMask_Uplink();
        }
        uint8_t residue = (uint8_t)(cell_idx % 3u);
        if (!((mask >> residue) & 1u)) {
            return false;              /* ineligible → Skip → no guard */
        }
        /* Eligible: Tx if residue matches hop_count, else Rx */
        uint8_t hop = MAC_GetHopCount();
        return (residue != (hop % 3u));
    }

    default:
        return true;  /* conservative: apply guard when uncertain */
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void TdmaMachine_Init(const TdmaPlatform_t *platform)
{
    TdmaTable_Init();
    s_platform          = *platform;
    s_cursor.phase_index = 0u;
    s_cursor.cell_index  = 0u;
    s_cursor.slot_index  = 0u;
    s_slot_idx           = 0u;
    s_slot_start_ms      = 0u;
    s_expected_wake_ms   = 0u;
    s_cursor_suspect     = false;
    s_bootstrapped_in_slot = false;
    s_running            = false;

    const Phase_t *p0 = TdmaTable_GetPhase(0u);
    s_cursor.slot_pos = (p0 != NULL && p0->header.duration_ms > 0u)
                 ? SLOT_POS_HEADER : SLOT_POS_CELL;
}

bool TdmaMachine_Start(void)
{
    if (MAC_GetClockState() == CLOCK_COLD) {
        enter_scanning("boot");
        return false;
    }
    /* Never cold (C3): the chain starts now, from {0,0,0}. */
    s_slot_start_ms    = s_platform.GetRtcMs();
    s_expected_wake_ms = s_slot_start_ms;
    s_running          = true;
    return true;
}

void TdmaMachine_OnRxEnd(void)
{
    if (MAC_GetClockState() == CLOCK_COLD) {
        if (s_running) {
            enter_scanning("lost");  /* Tier 3 re-anchor on this packet */
        } else {
            s_platform.RadioScan();  /* keep scanning */
        }
        return;
    }
    /* Synced slot: nothing more to receive until the next slot. */
    s_platform.RadioSleep();

    /* The next wake was programmed at the start of this slot, before its
     * packet arrived. Re-decide it now that the MAC has seen the packet:
     * e.g. a Tier 1 epoch turns C2's next Sync cell from Rx into Tx, which
     * needs no guard. The Rx window ends by slot end + MAX_GUARD_TIME_MS and
     * gaps are >= 2 x MAX_GUARD_TIME_MS, so the early wake is still ahead. */
    if (s_running) {
        uint32_t wake_ms = next_wake_ms();
        if (wake_ms != s_expected_wake_ms) {
            ARCLOG(ARCLOG_MOD_TDMA, VLEVEL_M, "WAKE_ADJ", "from=%u to=%u",
                   (unsigned)s_expected_wake_ms, (unsigned)wake_ms);
            s_expected_wake_ms = wake_ms;
            s_platform.ProgramAlarmA(wake_ms);
        }
    }
}

void TdmaMachine_SlotTask(void)
{
    uint32_t      now_ms;
    const Phase_t *phase;
    uint32_t      freq_hz;
    SlotDecision_t decision;
    uint32_t       next_start_ms;
    uint32_t       alarm_ms;

    /* Stale wake: the chain stopped (radio ISR entered scanning) while this
     * alarm's task was pending. Leave the scanning radio alone. */
    if (!s_running) return;

    /* ---- Step 1: read RTC ---- */
    s_bootstrapped_in_slot = false;
    now_ms = s_platform.GetRtcMs();

    /* ---- Step 2: cursor integrity checkpoint ---- */
    {
        phase = TdmaTable_GetPhase(s_cursor.phase_index);
        uint32_t threshold = (phase != NULL)
                             ? (phase->slot_active_ms + (phase->slot_active_ms >> 1u))
                             : 3750u;
        if (u32_abs_diff(now_ms, s_expected_wake_ms) > threshold) {
            s_cursor_suspect = true;
            ARCLOG(ARCLOG_MOD_TDMA, VLEVEL_L, "SLOT_SUSPECT", "exp=%u now=%u",
                   (unsigned)s_expected_wake_ms, (unsigned)now_ms);
            /* Drive MAC toward re-acquisition without touching the RTC. */
            MAC_OnCursorSuspect();
            /* Resume the alarm chain from the actual wake time; the old
             * nominal start is stale and the next alarm computed from it
             * could already be in the past (it would then only fire after
             * the RTC wraps at midnight). */
            s_slot_start_ms = now_ms;
        } else {
            s_cursor_suspect = false;
        }
    }

    /* If BootstrapFromSync was called during the cursor integrity checkpoint,
     * the cursor and Alarm A have already been repositioned to the correct
     * cell in the new RTC domain. Skip the rest of this SlotTask invocation;
     * the alarm from BootstrapFromSync will fire and resume normal execution. */
    if (s_bootstrapped_in_slot) return;

    /* The suspect cursor dropped the clock: stop the chain, scan. */
    if (MAC_GetClockState() == CLOCK_COLD) {
        enter_scanning("lost");
        return;
    }

    /* ---- Step 3: fetch phase ---- */
    phase = TdmaTable_GetPhase(s_cursor.phase_index);
    if (phase == NULL) {
        /* Should not happen with a valid table; program alarm to advance */
        alarm_ms = s_slot_start_ms + 3000u;
        s_expected_wake_ms = alarm_ms;
        s_platform.ProgramAlarmA(alarm_ms);
        return;
    }

    /* ---- Step 4: participant mask check ---- */
    if (!(phase->participant_mask & NODE_CLASS_BIT)) {
        /* Phase skipped — jump to the next participating phase */
        uint32_t phase_start = TdmaTable_PhaseStartOffset_ms(s_cursor.phase_index);
        skip_to_participating_phase();

        /* Program alarm at the start of the skipped-over phase duration */
        alarm_ms = s_slot_start_ms + phase->slot_active_ms + phase->gap_after_slot_ms;
        s_slot_start_ms    = alarm_ms;
        s_expected_wake_ms = alarm_ms;
        s_platform.ProgramAlarmA(alarm_ms);
        (void)phase_start;  /* used by phase_skip optimisation in full impl */
        return;
    }

    /* ---- Step 5-6: frequency and channel ---- */
    freq_hz = FrequencyResolver_GetFreq(&s_cursor);
    s_platform.RadioSetChannel(freq_hz);

    /* ---- Step 6.5: sync silence timeout check ---- */
    MAC_CheckSyncTimeout(now_ms);
    if (MAC_GetClockState() == CLOCK_COLD) {
        enter_scanning("lost");
        return;
    }

    /* ---- Step 7: MAC decision ---- */
    decision = MAC_OnSlotOpportunity(&s_cursor, phase);
    /* wake = actual minus programmed wake time: local timing deviation. */
    ARCLOG(ARCLOG_MOD_TDMA, VLEVEL_H, "SLOT",
           "ph=%u ty=%s ce=%u sl=%u pos=%s dec=%s wake=%d nom=%u",
           (unsigned)s_cursor.phase_index, ArcLog_PhaseTypeName(phase->type),
           (unsigned)s_cursor.cell_index, (unsigned)s_cursor.slot_index,
           ArcLog_SlotPosName(s_cursor.slot_pos), ArcLog_DecisionName(decision),
           (int)(int32_t)(now_ms - s_expected_wake_ms),
           (unsigned)s_slot_start_ms);

    /* ---- Step 8: radio action ---- */
    if (decision == SLOT_TX) {
        /* If we woke early (guard applied) and MAC decided Tx, delay to
         * nominal slot start before transmitting. */
        if (s_platform.WaitUntilMs != NULL && now_ms < s_slot_start_ms) {
            s_platform.WaitUntilMs(s_slot_start_ms);
        }
        ComplianceResult_t result =
            ComplianceEngine_RequestChannel(freq_hz,
                                            phase->slot_active_ms,
                                            TX_POWER_DBM);
        if (result != COMPLIANCE_GRANTED) {
            ARCLOG(ARCLOG_MOD_TDMA, VLEVEL_M, "TX_DENIED", "res=%u freq=%u",
                   (unsigned)result, (unsigned)freq_hz);
        }
        if (result == COMPLIANCE_GRANTED) {
            SyncPayload_t pkt;
            memset(&pkt, 0, sizeof(pkt));
            pkt.ms_since_midnight_sync_phase = MAC_GetSyncPhaseEpochMs();
            MAC_GetSyncPhaseDate(&pkt.day, &pkt.month, &pkt.year);
            pkt.sync_phase_index = (uint8_t)s_cursor.phase_index;
            pkt.sync_cell_index  = (uint8_t)s_cursor.cell_index;
            uint32_t send_ms = s_platform.GetRtcMs();
            s_platform.RadioSend((const uint8_t *)&pkt, (uint8_t)sizeof(pkt));
            /* plan = nominal slot start, send = RTC just before Radio.Send.
             * The radio's own TX start is logged by TX_DONE (end - toa). */
            ARCLOG(ARCLOG_MOD_SYNC, VLEVEL_M, "SYNC_TX",
                   "ph=%u ce=%u ep=%u plan=%u send=%u freq=%u",
                   (unsigned)pkt.sync_phase_index, (unsigned)pkt.sync_cell_index,
                   (unsigned)pkt.ms_since_midnight_sync_phase,
                   (unsigned)s_slot_start_ms, (unsigned)send_ms,
                   (unsigned)freq_hz);
            uint32_t actual_toa = s_platform.RadioTimeOnAir((uint8_t)sizeof(pkt));
            ComplianceEngine_ReportTxDone(freq_hz, actual_toa);
        }
        /* On non-GRANTED: fall through to alarm programming without TX */
    } else if (decision == SLOT_RX) {
        open_rx_window(phase, now_ms);
    } else {
        s_platform.RadioSleep();
    }

    /* ---- Steps 9-14: advance cursor, compute and program next alarm ---- */
    advance_cursor(phase);

    next_start_ms   = s_slot_start_ms + phase->slot_active_ms
                      + phase->gap_after_slot_ms;
    s_slot_start_ms = next_start_ms;

    alarm_ms = next_wake_ms();
    s_expected_wake_ms = alarm_ms;
    s_platform.ProgramAlarmA(alarm_ms);
}

void TdmaMachine_BootstrapFromSync(uint8_t  sync_phase_idx,
                                    uint8_t  sync_cell_idx,
                                    uint32_t nominal_start_ms)
{
    s_cursor.phase_index = sync_phase_idx;
    s_cursor.cell_index  = sync_cell_idx;
    s_cursor.slot_index  = 0u;
    s_slot_idx           = 0u;
    s_cursor.slot_pos     = SLOT_POS_CELL;
    s_slot_start_ms      = nominal_start_ms;
    s_cursor_suspect     = false;

    const Phase_t *phase = TdmaTable_GetPhase(sync_phase_idx);
    if (phase != NULL) {
        uint32_t next_ms = nominal_start_ms + phase->slot_active_ms
                           + phase->gap_after_slot_ms;
        advance_cursor(phase);
        s_slot_start_ms    = next_ms;

        /* Apply guard: the next slot will be Rx (acquisition packets 2+),
         * so wake early to open the window before nominal start. */
        s_expected_wake_ms = next_ms - GuardTimeResolver_GetGuardMs();
        s_platform.ProgramAlarmA(next_ms - GuardTimeResolver_GetGuardMs());
        s_bootstrapped_in_slot = true;
        s_running              = true;
    }
}

FrameCursor_t TdmaMachine_GetCursor(void)       { return s_cursor;          }
bool          TdmaMachine_IsCursorSuspect(void) { return s_cursor_suspect; }
