/*!
 * \file      tdma_table.h
 *
 * \brief     TDMA Table accessor — read-only, node-agnostic schedule
 *            descriptor for one Frame.
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
#ifndef TDMA_TABLE_H
#define TDMA_TABLE_H

#include <stdbool.h>
#include "protocol_types.h"

/* =========================================================================
 * Accessor interface
 *
 * The TDMA Table is a const array in flash. These three functions are the
 * only interface the TDMA Machine uses — it never touches the underlying
 * array directly, keeping the table swappable at link time.
 * ========================================================================= */

/*!
 * \brief   Return a pointer to the Phase at the given index.
 *
 * \param   [in]  phase_index - Zero-based index into the TDMA Table.
 *
 * \retval  const \ref Phase_t* Pointer to the Phase descriptor, or
 *          \c NULL if \c phase_index >= \ref TdmaTable_PhaseCount().
 */
const Phase_t *TdmaTable_GetPhase( uint8_t phase_index );

/*!
 * \brief   Return the total number of phases in the current table.
 *
 * \retval  uint8_t Number of phases (always >= 1 for a valid table).
 */
uint8_t TdmaTable_PhaseCount( void );

/*!
 * \brief   Return the precomputed millisecond offset of a Phase from the
 *          start of the Frame.
 *
 * \details Used by the TDMA Machine to jump the \ref FrameCursor_t directly
 *          to the next active Phase during long inter-phase sleep gaps,
 *          avoiding slot-by-slot iteration through excluded Phases.
 *
 * \param   [in]  phase_index - Zero-based index into the TDMA Table.
 *
 * \retval  uint32_t Millisecond offset from Frame start, or 0 if
 *          \c phase_index >= \ref TdmaTable_PhaseCount().
 */
uint32_t TdmaTable_PhaseStartOffset_ms( uint8_t phase_index );

/*!
 * \brief   Validate the "Sync cells are single-slot" structural invariant
 *          against an explicit phase array.
 *
 * \details \c SyncPayload_t carries no field identifying which slot within
 *          a cell a packet was received in — \c sync_cell_index is the only
 *          position field on the wire. \c TdmaMachine_BootstrapFromSync
 *          therefore always re-anchors to slot 0 of the received cell,
 *          which is only correct if every \ref PHASE_TYPE_SYNC phase has
 *          \c slot_count == 1. Takes an explicit array (rather than reading
 *          the module-static table) so it can be unit-tested against a
 *          deliberately malformed table without touching the real one.
 *
 * \param   [in] phases - Array of phase pointers to check. A \c NULL entry
 *                        is skipped.
 * \param   [in] count  - Number of entries in \c phases.
 *
 * \retval  bool \c true if every \c PHASE_TYPE_SYNC entry has
 *          \c slot_count == 1 (or none are present); \c false otherwise.
 */
bool TdmaTable_ValidateSyncSingleSlot_Of(const Phase_t * const *phases, uint8_t count);

/*!
 * \brief   Validate the "Sync cells are single-slot" invariant against the
 *          linked TDMA Table.
 *
 * \details Call once at boot, before \c TdmaMachine_Init, so a malformed
 *          table is caught before any Sync packet can be processed.
 *
 * \retval  bool See \ref TdmaTable_ValidateSyncSingleSlot_Of.
 */
bool TdmaTable_ValidateSyncSingleSlot(void);

#endif /* TDMA_TABLE_H */
