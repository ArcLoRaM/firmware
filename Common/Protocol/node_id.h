/*!
 * \file      node_id.h
 *
 * \brief     Node ID: the one-byte identity a node carries in its packets,
 *            looked up from the MCU 96-bit unique ID in a compiled table.
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
 * \details   Every board runs the same build of its node class; the Node ID
 *            is found at run time by matching the board's UID against
 *            \ref NodeId_Table. A board missing from the table gets
 *            \ref NODE_ID_UNPROVISIONED; its CM0+ BOOT line (id=0 uid=...)
 *            gives the UID to register (see ADR-0017).
 *
 *            Compiled for both cores and for host tests: the lookup is pure,
 *            only \ref NodeId_ReadUid touches the hardware.
 */
#ifndef NODE_ID_H
#define NODE_ID_H

#include <stddef.h>
#include <stdint.h>

/*! Words in the MCU unique ID (96 bits at UID_BASE, 0x1FFF7590). */
#define NODE_UID_WORDS          3u

/*! Board not in \ref NodeId_Table. */
#define NODE_ID_UNPROVISIONED   0x00u

/*! Reserved for broadcast, never assigned. */
#define NODE_ID_BROADCAST       0xFFu

/*! One registered board. uid is in address order: w0 at UID_BASE. */
typedef struct {
    uint32_t uid[NODE_UID_WORDS];
    uint8_t  node_id;
} NodeIdEntry_t;

/*!
 * \brief   The registered boards.
 * \param   count  Out: number of entries.
 */
const NodeIdEntry_t *NodeId_Table(size_t *count);

/*!
 * \brief   Node ID of a UID in \p table, or \ref NODE_ID_UNPROVISIONED.
 */
uint8_t NodeId_Find(const NodeIdEntry_t *table, size_t count,
                    const uint32_t uid[NODE_UID_WORDS]);

/*! Node ID of a UID in the registered table. */
uint8_t NodeId_FromUid(const uint32_t uid[NODE_UID_WORDS]);

#ifndef HOST_TEST
/*! Read this MCU's unique ID, w0 first. */
void    NodeId_ReadUid(uint32_t uid[NODE_UID_WORDS]);

/*! Node ID of this board. */
uint8_t NodeId_Self(void);
#endif

#endif /* NODE_ID_H */
