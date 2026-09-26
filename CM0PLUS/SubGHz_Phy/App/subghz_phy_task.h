/*!
 * \file      subghz_phy_task.h
 *
 * \brief     SubGHz PHY task registration — initialises all CM0+ protocol
 *            machines and registers the TDMA slot task with UTIL_SEQ.
 *
 * \details   Call \ref SubGhzPhyTask_Init once from \c MX_SubGHz_Phy_Init,
 *            after \c SystemApp_Init.  All machines (ComplianceEngine,
 *            FrequencyResolver, MAC State Machine, TDMA Machine) are
 *            initialised in dependency order, and \c TdmaMachine_SlotTask
 *            is registered and armed as \c CFG_SEQ_Task_TdmaSlotWake.
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
#ifndef SUBGHZ_PHY_TASK_H
#define SUBGHZ_PHY_TASK_H

/*!
 * \brief   Initialise all CM0+ protocol machines and arm the TDMA slot task.
 *
 * \details Initialisation order:
 *          1. ComplianceEngine
 *          2. FrequencyResolver
 *          3. MAC State Machine
 *          4. TDMA Machine
 *          5. UTIL_SEQ task registration and first-slot arm
 *
 *          Must be called once at boot, after \c SystemApp_Init.
 *          Safe to call from any context before the sequencer loop starts.
 */
void SubGhzPhyTask_Init(void);

/*!
 * \brief   Radio IRQ entry hook: stamps the RTC for the pending radio IRQs.
 *
 * \details Must be called first thing in SUBGHZ_Radio_IRQHandler, before
 *          HAL_SUBGHZ_IRQHandler clears the IRQ status. Records the RTC time
 *          of the IRQ (RxDone gives the SyncStamp) and, when set, of
 *          IRQ_PREAMBLE_DETECTED and IRQ_HEADER_VALID (diagnostics).
 */
void SubGhzPhyTask_OnRadioIrq(void);

#endif /* SUBGHZ_PHY_TASK_H */
