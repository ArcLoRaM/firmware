/*!
 * \file      compliance_engine.h
 *
 * \brief     Compliance Engine — gates every CM0+ transmission through
 *            ETSI EN 300 220 duty-cycle time-credit accounting.
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
#ifndef COMPLIANCE_ENGINE_H
#define COMPLIANCE_ENGINE_H

#include <stdint.h>
#include "shared_mem.h"

/*!
 * \brief   Inject dependencies and reset all per-band credit to the
 *          configured maximum.
 *
 * \details Call once at boot, supplying the inter-core shared-memory region
 *          that \ref ComplianceEngine_RequestChannel() writes after every
 *          call, and a monotonic millisecond tick source used for credit
 *          regeneration.
 *
 *          On the real device \c get_tick_ms should point to the CM0+ timer
 *          tick function (e.g. \c GetTimerTicks or equivalent from the
 *          SubGHz_Phy middleware or \c HAL_GetTick).
 *          In host unit tests supply a local counter so time can be
 *          controlled deterministically.
 *
 * \param   [in] status       - Pointer to the \ref ComplianceStatus_t region
 *                              in inter-core shared SRAM2. Must remain valid
 *                              for the lifetime of the module. May be NULL
 *                              (status writes are silently skipped).
 *
 * \param   [in] get_tick_ms  - Monotonic millisecond counter. Must not be
 *                              NULL.
 */
void ComplianceEngine_Init(ComplianceStatus_t *status,
                           uint32_t (*get_tick_ms)(void));

/*!
 * \brief   Request permission to transmit on \c freq_hz.
 *
 * \details Looks up \c freq_hz in the compiled-in \ref RegionProfile.  If
 *          found, regenerates accumulated credit since the last call, then
 *          evaluates TX-power and duty-cycle limits.  Writes
 *          \ref ComplianceStatus_t to shared memory on every return path,
 *          including non-GRANTED results.
 *
 *          In debug firmware builds a \c freq_hz outside all declared bands
 *          triggers \c assert(0) — a fatal misconfiguration that is a
 *          regulatory certification risk.  In host test builds
 *          (\c HOST_TEST defined) and production builds (\c NDEBUG defined)
 *          \ref COMPLIANCE_BAND_UNKNOWN is returned silently and
 *          \ref ComplianceStatus_t::band_unknown_count is incremented.
 *
 * \param   [in] freq_hz         - Centre frequency of the requested
 *                                 transmission in Hz.
 *
 * \param   [in] expected_toa_ms - Conservative time-on-air estimate in ms
 *                                 (\c slot_active_ms from the TDMA Table).
 *                                 Deducted from the band credit bucket on
 *                                 \ref COMPLIANCE_GRANTED.
 *
 * \param   [in] tx_power_dbm    - Requested TX power in dBm.
 *
 * \retval  \ref ComplianceResult_t Result of the check:
 *          \ref COMPLIANCE_GRANTED,
 *          \ref COMPLIANCE_RESTRICTED,
 *          \ref COMPLIANCE_POWER_TOO_HIGH,
 *          \ref COMPLIANCE_BAND_UNKNOWN.
 */
ComplianceResult_t ComplianceEngine_RequestChannel(uint32_t freq_hz,
                                                    uint32_t expected_toa_ms,
                                                    int8_t   tx_power_dbm);

/*!
 * \brief   Report the actual time-on-air after a successful transmission.
 *
 * \details Called by the TDMA Machine after TX completes with the
 *          \c Radio.TimeOnAir() result.  Refunds the difference
 *          \c (expected_toa_ms − actual_toa_ms) to the band credit bucket,
 *          correcting the conservative pre-TX deduction made by
 *          \ref ComplianceEngine_RequestChannel.  If \c actual_toa_ms ≥
 *          the stored expected value, no refund is applied.  Credit is
 *          capped at the band maximum after the refund.
 *
 * \param   [in] freq_hz       - Same frequency passed to the preceding
 *                               \ref ComplianceEngine_RequestChannel call.
 *
 * \param   [in] actual_toa_ms - Actual time-on-air in ms as computed by
 *                               \c Radio.TimeOnAir() after packet assembly.
 */
void ComplianceEngine_ReportTxDone(uint32_t freq_hz, uint32_t actual_toa_ms);

#endif /* COMPLIANCE_ENGINE_H */
