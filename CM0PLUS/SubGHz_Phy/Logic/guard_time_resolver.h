/*!
 * \file      guard_time_resolver.h
 *
 * \brief     Guard Time Resolver — provides the RX window guard margin
 *            that absorbs accumulated RTC clock drift since the last
 *            Frame Epoch correction.
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
#ifndef GUARD_TIME_RESOLVER_H
#define GUARD_TIME_RESOLVER_H

#include <stdint.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

/*!
 * \brief   Maximum guard time in milliseconds.
 *
 * \details This is the sole reference for guard time in the system.
 *          It is a property of the slot grid: the most any node may wake
 *          early. The TDMA Machine ends every Rx window at the latest packet
 *          start that still ends by slot end + MAX_GUARD_TIME_MS (the early
 *          wake itself uses \ref GuardTimeResolver_GetGuardMs, equal to this
 *          value in Version 1). The MAC State Machine uses it as the
 *          Tier-3 drift threshold: if a received Sync packet has a preamble
 *          offset error at or above this value, the node degrades to
 *          CLOCK_COLD immediately.
 *
 *          The TDMA Table gap constraint derives from this constant:
 *          every gap (\ref Phase_t.gap_after_slot_ms,
 *          \ref AnchorSlot_t.gap_after_ms) must be >= 2 * MAX_GUARD_TIME_MS
 *          to prevent Rx window overlap between adjacent slots.
 *
 *          Version 2 of the resolver will compute guard time from estimated
 *          clock drift, capped at this value:
 *          <c>min(estimated_drift_ms, MAX_GUARD_TIME_MS)</c>. If drift
 *          would require guard beyond this cap, the node degrades instead.
 *
 * \remark   Transient errors above this threshold may be tolerated in a
 *          future revision by splitting SYNC_RESYNC_THRESHOLD_MS into its
 *          own value. For now they are unified.
 */
#define MAX_GUARD_TIME_MS  200u  /* bench value: absorbs the ~100 ms preamble detection latency while it is uncompensated */

/*!
 * \brief   Tier-3 drift threshold: preamble offset error at or above
 *          this value triggers immediate CLOCK_COLD degradation.
 *
 * \details Aliased to \ref MAX_GUARD_TIME_MS. The unification creates a
 *          clean invariant: "if drift exceeds max guard, the node
 *          degrades." Breaking this alias in the future would allow
 *          tolerating transient errors above the guard cap.
 */
#define SYNC_RESYNC_THRESHOLD_MS  MAX_GUARD_TIME_MS

/* =========================================================================
 * Public API
 * ========================================================================= */

/*!
 * \brief   Return the current guard time in milliseconds.
 *
 * \details Version 1 returns \ref MAX_GUARD_TIME_MS. Version 2 will return
 *          <c>min(estimated_drift_ms, MAX_GUARD_TIME_MS)</c> based on time
 *          since last Sync correction and estimated clock drift.
 *
 * \retval  uint32_t Guard time in milliseconds.
 */
uint32_t GuardTimeResolver_GetGuardMs(void);

#endif /* GUARD_TIME_RESOLVER_H */
