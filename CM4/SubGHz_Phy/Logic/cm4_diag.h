/*!
 * \file      cm4_diag.h
 *
 * \brief     CM4 diagnostic aggregator — polls compliance status from shared
 *            SRAM2 and maintains per-link DiagnosticState for alarm detection.
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
#ifndef CM4_DIAG_H
#define CM4_DIAG_H

#include <stdbool.h>
#include <stdint.h>
#include "shared_mem.h"

/* =========================================================================
 * DiagType_t
 * ========================================================================= */

/*!
 * \brief   Diagnostic payload type codes originating from CM4.
 *
 * \details \c DIAG_ALERT (0x04) is the CM0+ emergency variant defined as a
 *          plain macro in shared_mem.h.  CM4 never originates a DIAG_ALERT,
 *          so it is intentionally omitted here to avoid macro/enum collision.
 */
typedef enum {
    DIAG_HEARTBEAT = 0x01u, /*!< Periodic health report from CM4. */
    DIAG_ALARM     = 0x02u, /*!< Threshold-triggered alarm from CM4. */
    DIAG_COHORT    = 0x03u, /*!< Neighbour cohort report; C2/C3 only. */
} DiagType_t;

/* =========================================================================
 * DiagnosticState_t
 * ========================================================================= */

/*!
 * \brief   CM4-local snapshot of compliance-related diagnostic counters.
 *
 * \details Populated by \ref CM4Diag_PollCompliance on every CM4 wake.
 *          \c diag_alarm_pending is a latching flag: once set by a non-zero
 *          \c band_unknown_count it remains true until CM4 explicitly resets
 *          it after the alarm payload has been enqueued for transmission.
 */
typedef struct {
    uint16_t skip_count_mesh;    /*!< Mirror of \ref ComplianceStatus_t::skip_count_mesh. */
    uint16_t skip_count_cluster; /*!< Mirror of \ref ComplianceStatus_t::skip_count_cluster. */
    bool     diag_alarm_pending; /*!< True when \c band_unknown_count > 0 was observed;
                                   *  latching — cleared manually after alarm is handled. */
} DiagnosticState_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/*!
 * \brief   Initialise the CM4 diagnostic aggregator.
 *
 * \details Zero-initialises internal \ref DiagnosticState_t.  Must be called
 *          once from CM4 init before the first \ref CM4Diag_PollCompliance call.
 */
void CM4Diag_Init(void);

/*!
 * \brief   Poll a \ref ComplianceStatus_t snapshot and update diagnostic state.
 *
 * \details Mirrors \c skip_count_mesh and \c skip_count_cluster to the internal
 *          \ref DiagnosticState_t.  If \c status->band_unknown_count is non-zero,
 *          sets \c diag_alarm_pending = true (latching).
 *
 * \param   [in] status - Pointer to the compliance status to inspect.
 *                        Passing \c &g_compliance_status reads directly from
 *                        SRAM2 on every CM4 wake.
 */
void CM4Diag_PollCompliance(const ComplianceStatus_t *status);

/*!
 * \brief   Return a read-only pointer to the current diagnostic state.
 *
 * \retval  const DiagnosticState_t* Pointer to the internal state; never NULL.
 */
const DiagnosticState_t *CM4Diag_GetState(void);

#endif /* CM4_DIAG_H */
