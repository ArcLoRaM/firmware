# ADR-0005: Sync Payload Uses Binary ms-Since-Midnight + BCD Date

## Status
Supersedes previous "Raw BCD + SSR" format (see git history for original).

## Context
The `Sync` Phase transmits a packet whose payload carries the Sync Phase Epoch —
the absolute start time of the current Sync Phase occurrence. Receivers use this
to set their RTC on cold start and to correct clock drift on every subsequent
occurrence. C3 is the sole epoch generator; C2 relays C3's received epoch verbatim.

Three format options were considered:

**Option A — Raw BCD + SSR** (previous format): transmit H:M:S as BCD plus raw
SSR register. Eliminated: SSR provides no useful precision (HAL_RTC_SetTime resets
it silently); BCD arithmetic is required on both TX (reading RTC) and RX sides for
any error computation; `GetTimerTicks()` already yields binary ms making BCD an
intermediate conversion with no benefit.

**Option B — Binary ms-since-midnight** (chosen for time): `uint32_t` ms since
midnight (0–86,399,999). TX path: `GetTimerTicks()` already returns this — zero
conversion. RX path: `target_ms = payload + sync_cell_index × per_cell_ms`, then
`H = ms/3600000`, `M = (ms%3600000)/60000`, `S = (ms%60000)/1000` — integer
division only, no BCD arithmetic, no SSR formula.

**Option C — Unix-style seconds + ms**: requires epoch reference and larger integer
types. Unnecessary for a network that tracks time-of-day only.

## Decision
`SyncPayload_t` (10 bytes) carries:
- `packet_type_id` (1 byte): packet type discriminator for future format evolution.
- `ms_since_midnight_sync_phase` (4 bytes, binary): sync phase start time in ms
  since midnight. C3 writes `GetTimerTicks()` at phase entry. C2 relays C3's
  received value unchanged.
- `day`, `month`, `year` (3 bytes, BCD): date passed through from `HAL_RTC_GetDate`
  with no conversion. `HAL_RTC_SetDate` accepts BCD directly.
- `sync_phase_index` (1 byte): TDMA Table phase index. Receiver uses it to look up
  `per_cell_ms` and bootstrap the FrameCursor in multi-Sync-phase frames.
- `sync_cell_index` (1 byte): cell within the Sync Phase. Receiver computes
  `target_ms = ms_since_midnight_sync_phase + sync_cell_index × per_cell_ms`.

Rationale:
1. **Zero TX conversion.** `GetTimerTicks()` already returns ms-since-midnight in
   binary. No BCD read or SSR formula required on the transmit path.
2. **Trivial RX decomposition.** Integer division only. No BCD arithmetic, no
   per-field carry propagation.
3. **C3-only epoch authority.** C2 never reads its own RTC for the payload epoch;
   it forwards C3's value. This eliminates inter-node epoch divergence under drift.
4. **Multi-phase frame support.** `sync_phase_index` lets a scanning node bootstrap
   its FrameCursor to the correct phase regardless of how many Sync Phases the
   frame contains.

## Consequences
Struct is 10 bytes (down from 11). Existing `sync_slot_index` field renamed to
`sync_cell_index` (semantics unchanged; cell == slot in Sync phases with 1 slot/cell).

**Known limitation — midnight rollover:** if `ms_since_midnight_sync_phase +
sync_cell_index × per_cell_ms ≥ 86,400,000`, the BCD date in the payload is from
the previous day and the receiver must advance the date by one day after setting
the time. Direct BCD calendar arithmetic is non-trivial (variable month lengths,
leap years). `HAL_RTC_DST_Add1Hour` handles the complexity natively (24 calls =
+1 day). Deferred to a future issue; extremely rare in practice (requires a Sync
Phase to straddle midnight and the cell offset to push past 00:00:00).

SSR sub-second correction: ongoing drift beyond `SYNC_PARTICIPATE_THRESHOLD_MS`
but below `SYNC_RESYNC_THRESHOLD_MS` (8ms–300ms) is corrected via
`HAL_RTCEx_SetSynchroShift` without a full RTC reset (three-tier dispatch;
see CONTEXT.md Sync Algorithm).
