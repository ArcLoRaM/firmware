/*!
 * \file      shared_mem.h
 *
 * \brief     Inter-core shared SRAM2 structure definitions for the
 *            ArcLoRaM STM32WL55 dual-core firmware.
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
#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <stdint.h>
#include "protocol_types.h"

/* =========================================================================
 * Ownership conventions
 *
 *   CM4  writes, CM0+ reads  — AlarmBRequest_t, FrequencyResolverState_t
 *   CM0+ writes, CM4  reads  — ComplianceStatus_t
 *
 * Note: CellEligibilityMask_t and PhaseTxFlag_t are CM0+-internal values
 * (written by the MAC State Machine, read by the TDMA Machine on the same
 * core). They are defined in mac_types.h and live in CM0+ RAM, not
 * in this inter-core shared region.
 *
 * All structs use natural alignment — no __packed — to avoid unaligned
 * load/store faults on CM0+ (Cortex-M0+ has no hardware unaligned access).
 * ========================================================================= */

/* =========================================================================
 * AlarmBRequest_t
 * ========================================================================= */

/*!
 * \brief   Request written by CM4 asking CM0+ to program RTC Alarm B.
 *
 * \details CM4 writes the BCD time fields then sets \c pending = 1.
 *          CM0+ acts only when \c pending == 1: programs Alarm B, then
 *          clears \c pending. This two-phase write eliminates the race where
 *          CM0+ reads a partially-written or stale request.
 *
 *          \c pending is a \c uint8_t at offset 0 — naturally atomic on
 *          ARM Cortex-M. No mutex required for the flag itself.
 *
 *          Layout (12 bytes, naturally aligned):
 *          \code
 *            offset 0 : pending   (uint8_t) — atomic flag
 *            offset 1 : hours     (uint8_t, BCD)
 *            offset 2 : minutes   (uint8_t, BCD)
 *            offset 3 : seconds   (uint8_t, BCD)
 *            offset 4 : day       (uint8_t, BCD)
 *            offset 5 : month     (uint8_t, BCD)
 *            offset 6 : year      (uint8_t, BCD)
 *            offset 7 : _reserved (uint8_t, alignment pad)
 *            offset 8 : subseconds (uint32_t, raw SSR)
 *          \endcode
 */
typedef struct {
    uint8_t  pending;    /*!< Atomic flag: 1 = request pending, 0 = consumed. */
    uint8_t  hours;      /*!< BCD 00–23. */
    uint8_t  minutes;    /*!< BCD 00–59. */
    uint8_t  seconds;    /*!< BCD 00–59. */
    uint8_t  day;        /*!< BCD 01–31. */
    uint8_t  month;      /*!< BCD 01–12. */
    uint8_t  year;       /*!< BCD 00–99, years since 2000. */
    uint8_t  _reserved;  /*!< Explicit alignment pad — do not use. */
    uint32_t subseconds; /*!< Raw SSR (counts DOWN from PREDIV_S). */
} AlarmBRequest_t;       /* 12 bytes */

/* =========================================================================
 * FrequencyResolverState_t
 * ========================================================================= */

#ifndef FREQ_MAX_PHASES
/*!
 * Maximum number of Phases in the TDMA Table supported by the Frequency
 * Resolver shared memory layout.
 *
 * \remark Sized for the expected full frame layout. Increasing this value
 *         grows FrequencyResolverState_t by ~140 bytes per extra phase.
 */
#define FREQ_MAX_PHASES           7u
#endif

#ifndef FREQ_MAX_CELLS_PER_PHASE
/*!
 * Maximum number of Cells per Phase supported by the
 * \ref CELL_FREQ_OVERRIDE per-cell frequency table.
 *
 * \remark Matches Phase_t::cell_count upper bound (0–32).
 */
#define FREQ_MAX_CELLS_PER_PHASE  32u
#endif

/*!
 * \brief   Cell-frequency assignment mode for one Phase.
 */
typedef enum {
    CELL_FREQ_STATIC,    /*!< Fixed frequency for all cells in the Phase. */
    CELL_FREQ_HOP,       /*!< LFSR hop sequence seeded by \c hop_seed; advanced
                           *  by CM0+ Frequency Resolver on each cell. */
    CELL_FREQ_OVERRIDE,  /*!< Per-cell table written by CM4; indexed by
                           *  \ref FrameCursor_t::cell_index. */
} CellFreqMode_t;

/*!
 * \brief   Per-Phase frequency configuration written by CM4.
 *
 * \details CM4 is the sole writer. CM0+ Frequency Resolver is read-only.
 *          Header and footer use independent fixed frequencies. Cell
 *          frequency follows \c cell_mode.
 */
typedef struct {
    uint32_t       header_freq_hz;  /*!< Header slot frequency in Hz; 0 = absent. */
    uint32_t       footer_freq_hz;  /*!< Footer slot frequency in Hz; 0 = absent. */
    CellFreqMode_t cell_mode;       /*!< Cell-frequency assignment mode. */
    union {
        uint32_t static_freq_hz;                      /*!< \ref CELL_FREQ_STATIC. */
        uint32_t hop_seed;                             /*!< \ref CELL_FREQ_HOP initial seed. */
        uint32_t override[FREQ_MAX_CELLS_PER_PHASE];  /*!< \ref CELL_FREQ_OVERRIDE per-cell table. */
    } cell;
} PhaseFrequency_t;

/*!
 * \brief   Complete Frequency Resolver shared memory region (~980 bytes).
 *
 * \details Written by CM4 at boot and on topology changes. CM0+ reads it
 *          synchronously from the Frequency Resolver on every slot boundary.
 *          Placed in SRAM2 at a fixed linker-assigned address.
 */
typedef struct {
    PhaseFrequency_t phases[FREQ_MAX_PHASES];  /*!< Indexed by phase_index. */
} FrequencyResolverState_t;

/* =========================================================================
 * ComplianceStatus_t
 * ========================================================================= */

/*!
 * \brief   Compliance Engine result codes returned by
 *          \c ComplianceEngine_RequestChannel().
 */
typedef uint8_t ComplianceResult_t;

#define COMPLIANCE_GRANTED        0u  /*!< TX authorised; credit deducted. */
#define COMPLIANCE_RESTRICTED     1u  /*!< Insufficient duty-cycle credit. */
#define COMPLIANCE_POWER_TOO_HIGH 2u  /*!< TX power exceeds band limit. */
#define COMPLIANCE_BAND_UNKNOWN   3u  /*!< Frequency outside all declared Bands. */

/*!
 * \brief   Compliance Engine status written to shared memory after every
 *          \c ComplianceEngine_RequestChannel() call.
 *
 * \details CM0+ is the sole writer. CM4 reads opportunistically on every
 *          wake — no dedicated MbMux signal is needed, avoiding channel
 *          saturation for frequent compliance events.
 *
 * \remark  The original PRD estimated this struct at 12 bytes, predating
 *          the final field list. Actual natural size is 20 bytes.
 */
typedef struct {
    uint32_t           last_freq_hz;        /*!< Frequency of the last RequestChannel call. */
    uint32_t           wait_ms;             /*!< Recovery time; valid when
                                              *  \c last_result == \ref COMPLIANCE_RESTRICTED. */
    uint16_t           skip_count_mesh;     /*!< Cumulative skipped TX slots on mesh link. */
    uint16_t           skip_count_cluster;  /*!< Cumulative skipped TX slots on cluster link. */
    ComplianceResult_t last_result;         /*!< Result of the last RequestChannel call. */
    uint8_t            band_unknown_count;  /*!< Incremented on \ref COMPLIANCE_BAND_UNKNOWN;
                                              *  non-zero triggers a DIAG_ALARM from CM4. */
    uint8_t            _pad[2];             /*!< Explicit alignment pad — do not use. */
} ComplianceStatus_t;                       /* 20 bytes */

#endif /* SHARED_MEM_H */
