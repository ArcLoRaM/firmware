/*!
 * \file      tdma_machine.h
 *
 * \brief     TDMA Machine — alarm-chain timing engine on CM0+. Drives the
 *            slot-by-slot execution loop by integrating the TDMA Table,
 *            Frequency Resolver, MAC State Machine, and Compliance Engine.
 *            All hardware interactions are injected through \ref TdmaPlatform_t.
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
#ifndef TDMA_MACHINE_H
#define TDMA_MACHINE_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol_types.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

#ifndef GUARD_TIME_MS
/*!
 * RX window guard margin in milliseconds.
 *
 * \remark The TDMA Machine opens the receive window GUARD_TIME_MS before the
 *         nominal slot start and holds it GUARD_TIME_MS beyond the nominal end,
 *         absorbing accumulated clock drift since the last Frame Epoch correction.
 *         See CONTEXT.md — Guard Time.
 */
#define GUARD_TIME_MS  5u
#endif

#ifndef TX_POWER_DBM
/*!
 * TX power passed to \ref ComplianceEngine_RequestChannel on every TX slot.
 *
 * \remark Must not exceed the band's \c max_tx_power_dbm (14 dBm for the
 *         ETSI 868 MHz ISM profile). Full TX power management is deferred. Should probably lie inside the MAC State Machine eventually, but for now this is a convenient global constant.
 */
#define TX_POWER_DBM  14
#endif

/* =========================================================================
 * Platform abstraction
 * ========================================================================= */

/*!
 * \brief   Struct of hardware function pointers injected at
 *          \ref TdmaMachine_Init.
 *
 * \details All hardware interactions go through this struct, making
 *          \c tdma_machine.c completely free of HAL, radio-driver, and
 *          UTIL_* includes.  In production, construct one in
 *          \c app_subghz_phy.c pointing to real HAL wrappers.  In unit
 *          tests, construct one pointing to stubs.
 */
typedef struct {
    /*! Read the RTC time as a millisecond offset. */
    uint32_t (*GetRtcMs)(void);

    /*! Program RTC Alarm A to fire at the given absolute millisecond value. */
    void     (*ProgramAlarmA)(uint32_t abs_rtc_ms);

    /*! Tune the radio to the specified frequency in Hz. */
    void     (*RadioSetChannel)(uint32_t freq_hz);

    /*! Transmit \c len bytes from \c buf. */
    void     (*RadioSend)(const uint8_t *buf, uint8_t len);

    /*!
     * Open a receive window for \c timeout_ms milliseconds.
     * The window duration must already include any guard-time extension.
     */
    void     (*RadioSetRx)(uint32_t timeout_ms);

    /*! Put the radio into low-power sleep mode. */
    void     (*RadioSleep)(void);

    /*!
     * Return the actual time-on-air in milliseconds for the most recently
     * assembled packet.  Called after \c RadioSend to refund the difference
     * between the conservative pre-TX estimate and the real airtime to
     * \ref ComplianceEngine_ReportTxDone.
     */
    uint32_t (*RadioTimeOnAir)(void);
} TdmaPlatform_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/*!
 * \brief   Inject platform dependencies and reset the FrameCursor to
 *          \c {0,0,0}.
 *
 * \details Call once before the UTIL_SEQ scheduler starts.  The platform
 *          struct is copied internally — the caller's struct need not remain
 *          valid after the call.  The first \ref TdmaMachine_SlotTask
 *          invocation picks up from the first slot of the first phase.
 *
 * \param   [in] platform - Pointer to an initialised \ref TdmaPlatform_t.
 *                          Must not be NULL.
 */
void TdmaMachine_Init(const TdmaPlatform_t *platform);

/*!
 * \brief   Execute one slot of the TDMA alarm-chain loop.
 *
 * \details Registered as \c CFG_SEQ_Task_TdmaSlotWake; called by UTIL_SEQ
 *          on each RTC Alarm A wake.  Reads the TDMA Table, queries the
 *          Frequency Resolver and MAC State Machine, checks compliance,
 *          drives the appropriate radio action, advances the FrameCursor,
 *          and programs the next Alarm A before returning.
 */
void TdmaMachine_SlotTask(void);

/*!
 * \brief   Return a snapshot of the current FrameCursor position.
 *
 * \retval  \ref FrameCursor_t Live cursor (copy; not a pointer).
 */
FrameCursor_t TdmaMachine_GetCursor(void);

/*!
 * \brief   Return \c true if the RTC integrity checkpoint detected an
 *          implausible delta on the most recent \ref TdmaMachine_SlotTask
 *          entry.
 *
 * \details The flag is set when the actual RTC time deviates from the
 *          expected wake time by more than 1.5× the current
 *          \c slot_active_ms.  It is cleared on the next successful
 *          (plausible) SlotTask entry.
 *
 * \retval  bool \c true = cursor suspect; \c false = timing normal.
 */
bool TdmaMachine_IsCursorSuspect(void);

#endif /* TDMA_MACHINE_H */
