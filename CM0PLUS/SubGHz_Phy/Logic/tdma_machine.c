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
#include "sys_app.h"
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
static SlotPosition_t  s_slot_pos;
static uint8_t         s_slot_idx;      /* slot within the current cell */
static uint32_t        s_slot_start_ms; /* nominal start of the current slot */
static uint32_t        s_expected_wake_ms;
static bool            s_cursor_suspect;

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
    s_slot_pos = (p != NULL && p->header.duration_ms > 0u)
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
        s_slot_pos = (p0 != NULL && p0->header.duration_ms > 0u)
                     ? SLOT_POS_HEADER : SLOT_POS_CELL;
    } else {
        enter_phase(next);
    }
}

static void advance_cursor(const Phase_t *phase)
{
    switch (s_slot_pos) {
    case SLOT_POS_HEADER:
        s_slot_pos = SLOT_POS_CELL;
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
                s_slot_pos = SLOT_POS_FOOTER;
            } else {
                advance_to_next_phase();
            }
        }
        break;

    case SLOT_POS_FOOTER:
        advance_to_next_phase();
        break;

    default:
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
    s_slot_pos           = SLOT_POS_CELL;
    return false;
}

/* =========================================================================
 * Internal helpers — timing
 * ========================================================================= */

static uint32_t u32_abs_diff(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

/* Determine whether the next slot opportunity will be RX (for guard-time
   look-ahead).  Only handles DIRECTION_MAC_PHASE; other modes default false. */
static bool next_slot_is_rx(const Phase_t *phase)
{
    if (phase->direction_mode == DIRECTION_MAC_PHASE) {
        return (MAC_GetPhaseTxFlag() == 0u);
    }
    return false;  /* conservative: no guard time for other modes */
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void TdmaMachine_Init(const TdmaPlatform_t *platform)
{
    s_platform          = *platform;
    s_cursor.phase_index = 0u;
    s_cursor.cell_index  = 0u;
    s_cursor.slot_index  = 0u;
    s_slot_idx           = 0u;
    s_slot_start_ms      = 0u;
    s_expected_wake_ms   = 0u;
    s_cursor_suspect     = false;

    const Phase_t *p0 = TdmaTable_GetPhase(0u);
    s_slot_pos = (p0 != NULL && p0->header.duration_ms > 0u)
                 ? SLOT_POS_HEADER : SLOT_POS_CELL;
}

void TdmaMachine_SlotTask(void)
{
    uint32_t      now_ms;
    const Phase_t *phase;
    uint32_t      freq_hz;
    SlotDecision_t decision;
    uint8_t        prev_slot_idx;
    uint32_t       next_start_ms;
    uint32_t       alarm_ms;

    /* ---- Step 1: read RTC ---- */
    now_ms = s_platform.GetRtcMs();
    APP_LOG(TS_ON, VLEVEL_H, "TDMA: slot ph=%u cell=%u sl=%u now=%lu ms\r\n",
            (unsigned)s_cursor.phase_index, (unsigned)s_cursor.cell_index,
            (unsigned)s_cursor.slot_index, now_ms);

    /* ---- Step 2: cursor integrity checkpoint ---- */
    {
        phase = TdmaTable_GetPhase(s_cursor.phase_index);
        uint32_t threshold = (phase != NULL)
                             ? (phase->slot_active_ms + (phase->slot_active_ms >> 1u))
                             : 3750u;
        if (u32_abs_diff(now_ms, s_expected_wake_ms) > threshold) {
            s_cursor_suspect = true;
            /* Drive MAC toward re-acquisition */
            SyncPayload_t zero_pkt;
            memset(&zero_pkt, 0, sizeof(zero_pkt));
            MAC_OnSyncPacketReceived(&zero_pkt, now_ms);
        } else {
            s_cursor_suspect = false;
        }
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
        alarm_ms = s_slot_start_ms + phase->slot_active_ms + phase->gap_slots_ms[0];
        s_slot_start_ms    = alarm_ms;
        s_expected_wake_ms = alarm_ms;
        s_platform.ProgramAlarmA(alarm_ms);
        (void)phase_start;  /* used by phase_skip optimisation in full impl */
        return;
    }

    /* ---- Step 5-6: frequency and channel ---- */
    freq_hz = FrequencyResolver_GetFreq(&s_cursor, s_slot_pos);
    s_platform.RadioSetChannel(freq_hz);

    /* ---- Step 7: MAC decision ---- */
    decision = MAC_OnSlotOpportunity(&s_cursor, phase);
    APP_LOG(TS_ON, VLEVEL_H, "TDMA: MAC dec=%u mac_state=%u clock=%u\r\n",
            (unsigned)decision, (unsigned)MAC_GetState(),
            (unsigned)MAC_GetClockState());

    /* ---- Step 8: radio action ---- */
    if (decision == SLOT_TX) {
        ComplianceResult_t result =
            ComplianceEngine_RequestChannel(freq_hz,
                                            phase->slot_active_ms,
                                            TX_POWER_DBM);
        APP_LOG(TS_ON, VLEVEL_H, "TDMA: TX compliance result=%u freq=%lu\r\n",
                (unsigned)result, freq_hz);
        if (result == COMPLIANCE_GRANTED) {
            SyncPayload_t pkt;
            memset(&pkt, 0, sizeof(pkt));
            s_platform.RadioSend((const uint8_t *)&pkt, (uint8_t)sizeof(pkt));
            uint32_t actual_toa = s_platform.RadioTimeOnAir();
            ComplianceEngine_ReportTxDone(freq_hz, actual_toa);
        }
        /* On non-GRANTED: fall through to alarm programming without TX */
    } else if (decision == SLOT_RX) {
        s_platform.RadioSetRx(phase->slot_active_ms + 2u * GUARD_TIME_MS);
    } else {
        s_platform.RadioSleep();
    }

    /* ---- Steps 9-14: advance cursor, compute and program next alarm ---- */
    prev_slot_idx   = s_slot_idx;
    advance_cursor(phase);

    next_start_ms   = s_slot_start_ms + phase->slot_active_ms
                      + phase->gap_slots_ms[prev_slot_idx];
    s_slot_start_ms = next_start_ms;

    /* Guard-time look-ahead: if next slot will be RX, wake early */
    alarm_ms = next_start_ms;
    {
        const Phase_t *next_phase = TdmaTable_GetPhase(s_cursor.phase_index);
        if (next_phase != NULL && next_slot_is_rx(next_phase)) {
            alarm_ms -= GUARD_TIME_MS;
        }
    }

    s_expected_wake_ms = alarm_ms;
    s_platform.ProgramAlarmA(alarm_ms);
}

FrameCursor_t TdmaMachine_GetCursor(void)  { return s_cursor;          }
bool          TdmaMachine_IsCursorSuspect(void) { return s_cursor_suspect; }
