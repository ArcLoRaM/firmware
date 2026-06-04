/*!
 * \file      mac_state_machine.h
 *
 * \brief     MAC State Machine — per-opportunity decision layer on CM0+.
 *            Called synchronously by the TDMA Machine and by radio receive
 *            callbacks. Owns state transitions, three-packet Sync acquisition,
 *            CellEligibilityMask computation, phase_tx_flag management, and
 *            BeaconTxBudget gating.
 *
 * \details   Three separate compilation units implement this interface:
 *            \c mac_state_machine_c1.c (end node),
 *            \c mac_state_machine_c2.c (relay), and
 *            \c mac_state_machine_c3.c (gateway / SyncAnchor).
 *            Exactly one is linked per firmware image.
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
#ifndef MAC_STATE_MACHINE_H
#define MAC_STATE_MACHINE_H

#include <stdint.h>
#include "protocol_types.h"
#include "mac_types.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

#ifndef SYNC_LOCK_THRESHOLD_MS
/*!
 * Maximum preamble offset error that still counts as a valid Sync lock.
 *
 * \remark 0.5 × T_S at SF12/BW125 ≈ 16 ms — provides ~6× margin against
 *         the 3 × T_S preamble-locking failure ceiling (~100 ms).
 *         See CONTEXT.md — CT Sync Propagation Model.
 */
#define SYNC_LOCK_THRESHOLD_MS  16u
#endif

#ifndef BEACON_K_TX_CELLS
/*!
 * Number of cells per Mesh_Beacon phase in which this node transmits its
 * own BeaconPayload (when BeaconTxBudget allows).
 *
 * \remark Deterministic selection: the first K cells (indices 0…K−1) are
 *         the TX candidates. Randomisation is deferred pending empirical
 *         validation (see CONTEXT.md — BeaconTxBudget).
 */
#define BEACON_K_TX_CELLS  2u
#endif

#ifndef ROUTE_COST_CHANGE_THRESHOLD
/*!
 * Minimum absolute route_cost delta that counts as a structural routing
 * change and triggers a BeaconTxBudget reset to 2.
 */
#define ROUTE_COST_CHANGE_THRESHOLD  100u
#endif

/* =========================================================================
 * Injected hook struct
 * ========================================================================= */

/*!
 * \brief   Hardware and notification hooks injected at \ref MAC_Init.
 *
 * \details Any hook may be NULL; the MAC silently skips the call in that
 *          case. In production set each pointer to the real CM0+ function.
 *          In unit tests inject stub counters.
 */
typedef struct {
    /*!
     * Packet 1 hook — set the RTC from a pre-computed binary target time.
     *
     * \param target_ms  ms-since-midnight of the current cell's nominal start:
     *                   ms_since_midnight_sync_phase + sync_cell_index × per_cell_ms.
     *                   Caller decomposes: H = ms/3600000, M = (ms%3600000)/60000,
     *                   S = (ms%60000)/1000; calls HAL_RTC_SetTime(BIN) + SetDate(BCD).
     * \param day        BCD day 01–31 (pass-through from SyncPayload).
     * \param month      BCD month 01–12.
     * \param year       BCD year 00–99 (years since 2000).
     */
    void (*rtc_set)(uint32_t target_ms,
                    uint8_t  day, uint8_t month, uint8_t year);

    /*!
     * Tier 2 drift correction hook — CLOCK_WARM, 8ms ≤ error < 300ms.
     *
     * \details Applies HAL_RTCEx_SetSynchroShift without a full calendar
     *          re-anchor. Must not block. Called only from
     *          \ref MAC_OnSyncPacketReceived. Relay is suppressed for this
     *          sync occurrence regardless of the return path.
     *
     * \param preamble_timestamp_ms  DIO1 ISR timestamp (GetTimerTicks domain).
     * \param expected_offset_ms     ms_since_midnight_sync_phase +
     *                               sync_cell_index × per_cell_ms.
     */
    void (*rtc_align_subsecond)(uint32_t preamble_timestamp_ms,
                                 uint32_t expected_offset_ms);

    /*!
     * Atomic snapshot of the current RTC time and date.
     *
     * \details Called at Sync Phase entry (C3/C2 TX path) to capture the epoch
     *          for the outgoing SyncPayload, and immediately after \c rtc_set
     *          (C2 Packet 1 path) to read the new RTC domain. A single call
     *          avoids the race where two separate reads straddle midnight.
     *
     * \param[out] ms    GetTimerTicks() — ms since midnight in new RTC domain.
     * \param[out] day   BCD day 01–31.
     * \param[out] month BCD month 01–12.
     * \param[out] year  BCD year 00–99.
     */
    void (*get_rtc_snapshot)(uint32_t *ms,
                              uint8_t  *day, uint8_t *month, uint8_t *year);

    /*!
     * Packet 1 hook — called after the TDMA cursor has been re-anchored.
     *
     * \param sync_phase_idx  Phase index from \c SyncPayload.sync_phase_index.
     * \param sync_cell_idx   Cell index from \c SyncPayload.sync_cell_index.
     * \param slot_start_ms   Nominal start of the received cell in the new
     *                        RTC domain (= \c get_rtc_snapshot ms value).
     */
    void (*sync_bootstrapped)(uint8_t  sync_phase_idx,
                               uint8_t  sync_cell_idx,
                               uint32_t slot_start_ms);

    /*! Fired when ClockState transitions to CLOCK_WARM (2 consecutive good packets). */
    void (*sync_locked)(void);

    /*! Fired when ClockState degrades back to CLOCK_COLD. */
    void (*sync_lost)(void);
} MAC_Hooks_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/*!
 * \brief   Inject hooks and reset all MAC state to boot defaults.
 *
 * \details C1/C2: MacState = Scanning, ClockState = CLOCK_COLD.
 *          C3: MacState = Active (SyncAnchor — no acquisition needed).
 *          Call once at CM0+ startup before any slot opportunities fire.
 *
 * \param   [in] hooks - Pointer to hook struct. May be NULL (all hooks
 *                       silently skipped). Struct need not remain valid
 *                       after the call — values are copied internally.
 */
void MAC_Init(const MAC_Hooks_t *hooks);

/*!
 * \brief   TDMA Machine entry point — decide what to do at a slot opportunity.
 *
 * \details Called once per slot by the TDMA Machine after it wakes and
 *          determines this node participates in the current Phase.  The MAC
 *          writes \ref PhaseTxFlag_t at Sync phase entry and decrements
 *          \ref BeaconTxBudget on Beacon TX cells.
 *
 * \param   [in] cursor - Live TDMA position (phase_index, cell_index,
 *                        slot_index).
 * \param   [in] phase  - Phase descriptor for the current slot. Passed
 *                        directly by the TDMA Machine; the MAC must not
 *                        look it up again from the TDMA Table.
 *
 * \retval  \ref SlotDecision_t Radio action:
 *          \ref SLOT_TX, \ref SLOT_RX, or \ref SLOT_SKIP.
 */
SlotDecision_t MAC_OnSlotOpportunity(const FrameCursor_t *cursor,
                                      const Phase_t       *phase);

/*!
 * \brief   Called by the CM0+ RxDone wrapper when a Sync packet is decoded.
 *
 * \details Drives the three-packet ClockState acquisition state machine
 *          (C1/C2). C3 ignores this call (it is the SyncAnchor).
 *
 * \param   [in] payload              - Decoded SyncPayload from the received
 *                                      packet.
 * \param   [in] preamble_timestamp_ms - Millisecond timestamp captured in
 *                                      the DIO1 preamble ISR.
 */
void MAC_OnSyncPacketReceived(const SyncPayload_t *payload,
                               uint32_t             preamble_timestamp_ms);

/*!
 * \brief   Called by the CM0+ RxDone wrapper when a Beacon packet is decoded.
 *
 * \details Updates CellEligibilityMask (uplink and downlink formulas),
 *          evaluates BeaconTxBudget trigger, applies the anti-circular
 *          route filter, and transitions to Paired on first valid beacon.
 *
 * \param   [in] beacon - Decoded BeaconPayload from the received packet.
 */
void MAC_OnBeaconReceived(const BeaconPayload_t *beacon);

/*!
 * \brief   GPS / external sync acquisition hook — stub body only.
 *
 * \details Reserved for a future path where an external GPS provides the
 *          Frame Epoch directly, bypassing the three-packet Sync acquisition.
 *          Body is empty; the function must compile and link.
 *
 * \param   [in] epoch - Frame Epoch from the external time source.
 */
void MAC_OnExternalSyncAcquired(const FrameEpoch_t *epoch);

/*!
 * \brief   Return the current MAC operational state.
 *
 * \retval  \ref MacState_t Current state.
 */
MacState_t MAC_GetState(void);

/*!
 * \brief   Return the current RTC synchronisation quality.
 *
 * \retval  \ref ClockState_t Current clock state.
 */
ClockState_t MAC_GetClockState(void);

/*!
 * \brief   Return the uplink CellEligibilityMask last written by the MAC.
 *
 * \details Valid after the first \ref MAC_OnBeaconReceived call.
 *          Default 0x00 (skip all cells) before any beacon is received.
 *
 * \retval  \ref CellEligibilityMask_t Uplink mask.
 */
CellEligibilityMask_t MAC_GetCellEligibilityMask_Uplink(void);

/*!
 * \brief   Return the downlink CellEligibilityMask last written by the MAC.
 *
 * \retval  \ref CellEligibilityMask_t Downlink mask.
 */
CellEligibilityMask_t MAC_GetCellEligibilityMask_Downlink(void);

/*!
 * \brief   Return the phase_tx_flag last written by the MAC.
 *
 * \details C3: always 1. C1: always 0. C2: not pre-written (reactive model).
 *
 * \retval  \ref PhaseTxFlag_t Current flag value.
 */
PhaseTxFlag_t MAC_GetPhaseTxFlag(void);

/*!
 * \brief   Return the Sync Phase start time (binary ms) in the current RTC domain.
 *
 * \details Set on Packet 1: \c get_rtc_snapshot().ms − sync_cell_index × per_cell_ms.
 *          Valid after \ref MAC_OnSyncPacketReceived with \c CLOCK_COLD.
 *          Used for CLOCK_ACQUIRING elapsed-ms error computation.
 *
 * \retval  uint32_t Phase start in ms-since-midnight (current RTC domain).
 */
uint32_t MAC_GetSyncPhaseMs(void);

/*!
 * \brief   Return the Sync Phase Epoch captured for TX relay.
 *
 * \details C3: \c get_rtc_snapshot().ms captured at Sync Phase entry.
 *          C2: \c ms_since_midnight_sync_phase received in Cell 0 (Tier 1 only).
 *          Written into the outgoing \c SyncPayload_t.ms_since_midnight_sync_phase.
 *
 * \retval  uint32_t ms_since_midnight_sync_phase for the current occurrence.
 */
uint32_t MAC_GetSyncPhaseEpochMs(void);

/*!
 * \brief   Return the BCD date captured at Sync Phase entry.
 *
 * \details Populated by \c get_rtc_snapshot at phase entry (C3) or by the
 *          received SyncPayload date fields (C2 relay).
 *
 * \param[out] day   BCD day 01–31.
 * \param[out] month BCD month 01–12.
 * \param[out] year  BCD year 00–99.
 */
void MAC_GetSyncPhaseDate(uint8_t *day, uint8_t *month, uint8_t *year);

#endif /* MAC_STATE_MACHINE_H */
