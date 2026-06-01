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
     * Packet 1 hook — set the RTC calendar from BCD fields decoded from
     * the SyncPayload.  Sub-second accuracy is not yet established at this
     * point; the call only coarsely aligns the calendar.
     */
    void (*rtc_set)(uint8_t hours, uint8_t minutes, uint8_t seconds,
                    uint8_t day,   uint8_t month,   uint8_t year,
                    uint32_t subseconds);

    /*!
     * Packet 2 hook — align the RTC sub-second register.
     * \c preamble_timestamp_ms is the captured preamble arrival time;
     * \c expected_offset_ms is the computed expected offset from the
     * Frame Epoch for that sync_slot_index.
     */
    void (*rtc_align_subsecond)(uint32_t preamble_timestamp_ms,
                                 uint32_t expected_offset_ms);

    /*! Fired when ClockState transitions to CLOCK_WARM (third valid packet). */
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
 * \details Written at Sync phase entry. 1 = TX (participation cycle),
 *          0 = RX (audit cycle).
 *
 * \retval  \ref PhaseTxFlag_t Current flag value.
 */
PhaseTxFlag_t MAC_GetPhaseTxFlag(void);

#endif /* MAC_STATE_MACHINE_H */
