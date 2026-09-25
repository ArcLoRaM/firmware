# Wall-clock sync silence timeout for ClockState degradation

ClockState degradation uses a wall-clock timeout (15 minutes, provisioned) instead
of a miss-count-based mechanism.
When no Sync packet of any tier is received for 15 minutes, the node degrades to
`CLOCK_COLD` directly.
This decouples degradation from TDMA table structure, which was the core problem
with the count-based approach proposed in issue #16: a variable TDMA table makes
sync-cell frequency per unit of time unpredictable, so a miss count would couple
the clock-quality model to table layout.

## Considered Options

**A. Miss-count-based degradation (issue #16).** Count missed sync RX slots;
degrade after N misses. Rejected because the TDMA table varies, so the number of
sync cells per unit of time is not fixed. This would link degradation thresholds to
table structure, creating a maintenance coupling between two unrelated concerns.

**B. Wall-clock silence timeout (chosen).** Measure elapsed wall-clock time since
the last received Sync packet of any tier. Degrade after 15 minutes of silence.
Table-independent. Any received Sync packet (Tier 1, 2, or 3) resets the timer.
Tier 3 (received packet with error >= `SYNC_RESYNC_THRESHOLD_MS` = `MAX_GUARD_TIME_MS` = 100 ms) remains immediate, as before.

## Consequences

- **MAC role expansion.** The timeout check lives in the MAC state machine, which
  already owns `ClockState` and receives sync packets. This deviates from the MAC's
  original role (packet-driven state transitions) by adding a time-driven
  transition. Accepted as a trade-off for keeping all degradation logic in one
  module. If the MAC grows further time-driven responsibilities, consider
  extracting a dedicated clock-quality module.

- **RTC as sole timebase.** The platform has no free-running timer independent of
  the RTC (TIM2/LPTIM2 are available but unconfigured). The timeout is measured
  using the RTC, which is re-synced on every good packet. This is safe at
  15-minute scale: crystal drift (10-50 ppm) introduces at most a few hundred ms
  of error per minute, negligible against a 15-minute threshold. If tighter
  timeout guarantees are needed later, a free-running timer should be added.

- **CLOCK_ACQUIRING timeout.** The timeout fires from both `CLOCK_WARM` and
  `CLOCK_ACQUIRING`. A node stuck in `CLOCK_ACQUIRING` (received one sync packet
  but never confirmed) degrades to `CLOCK_COLD` after 15 minutes of silence,
  resetting to a clean scanning state.

- **Deferred: `sync_lost_reason` enum.** The `sync_lost` hook fires for both
  Tier 3 and timeout degradations. A `sync_lost_reason` parameter would let the
  diagnostic layer distinguish "drift" from "silence." Deferred — the cause is
  inferrable from context (time since last sync). Add if diagnostic value justifies
  it.

- **Deferred: cursor-suspect as degradation signal.** `s_cursor_suspect` in the
  TDMA machine detects alarm-timing anomalies (RTC jumped or drifted). A node
  sporadically re-acquiring is less harmful than one behaving on the wrong clock.
  Feeding cursor-suspect into ClockState degradation could catch drift-induced
  misalignment earlier. Deferred — it conflates alarm-timing anomalies with
  sync-quality tracking. Revisit if field data shows nodes behaving on wrong
  clocks without triggering Tier 3.

- **Dropped: per-missed-slot counting.** Issue #16 proposed a per-missed-slot
  count for diagnostics. Dropped — it reintroduces TDMA table coupling (you'd need
  to know which slots are sync-RX slots to count misses). The gateway can derive
  early-warning signal quality from periodic DIAG heartbeats instead.
