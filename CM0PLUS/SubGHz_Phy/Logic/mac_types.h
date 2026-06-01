/*!
 * \file      mac_types.h
 *
 * \brief     CM0+-internal MAC type aliases: CellEligibilityMask and
 *            PhaseTxFlag. These values are written by the MAC State Machine
 *            and read by the TDMA Machine — both on CM0+. They live in CM0+
 *            RAM, not in inter-core shared SRAM2.
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
#ifndef MAC_TYPES_H
#define MAC_TYPES_H

#include <stdint.h>

/*!
 * \brief   3-bit eligibility mask written by the MAC State Machine and read
 *          by the TDMA Machine at each cell boundary.
 *
 * \details Bit N = 1 means the node wakes for cells where
 *          \c cell_index \c % \c 3 \c == \c N. Two formulas exist — one per
 *          phase direction (uplink ascending, downlink mirror). Default 0x00
 *          (skip all cells) until the first \ref BeaconPayload_t is received.
 *          Used in all \ref DIRECTION_CELL_SKIP phases.
 */
typedef uint8_t CellEligibilityMask_t;

/*!
 * \brief   Whole-phase TX/RX flag written by the MAC State Machine before
 *          each Sync phase and read once by the TDMA Machine at phase entry.
 *
 * \details 1 = transmit every cell (participation cycle).
 *          0 = receive every cell (audit cycle).
 *          C3 (SyncAnchor) always writes 1. C1 always writes 0.
 *          C2 alternates between 0 and 1 on successive Sync phase entries.
 *          Used in \ref DIRECTION_MAC_PHASE phases only.
 *          Use Concurrent Transmission (CT) to achieve collision-less transmission.
 */
typedef uint8_t PhaseTxFlag_t;

#endif /* MAC_TYPES_H */
