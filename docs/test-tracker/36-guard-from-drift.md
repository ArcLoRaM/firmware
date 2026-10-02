# Issue #36: Rx guard from the drift estimate, on hardware

Purpose: acceptance. Issue: #36.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/guard-from-drift.toml` (to write once #34 and the guard are in) | C3 + C2 for several hours: every Sync packet the C3 sent (`TX_DONE`) is received or lost for a radio reason, never because the C2's window was not open on it; the guard of every Rx slot is logged (`g`, `win`); no Tier 3 or `SYNC_LOST` |

## Hardware

Checked against `bench boards` on 2026-10-02: no probe connected.

| Item | Needed |
|---|---|
| Node ? flashed as C3 | yes |
| Node ? flashed as C2 | yes |
| Host on AC power, Windows sleep off, for the several-hour run | yes |

Hands on the bench: plugging the two boards in.
No CubeMX regeneration.
The Rx start latency measurement (`timing-probe`) needs the same two boards.

## Criteria

- [ ] Host: guard from a stub estimator (valid and not valid, several residual rates, time since the last Sync), the cap, the ratio, and the fixed terms.
- [ ] Host: the threshold bands, including a 5 ms error that is corrected and still relays, and the unchanged 8 ms and resync behaviour.
- [ ] Host: the window end in `CLOCK_WARM` and while acquiring.
- [ ] Rx start latency measured with `timing-probe` and recorded here.
- [ ] `g` and `win` in the trace and the schema; schema test passes.
- [ ] Existing MAC and TDMA tests unchanged and passing.
- [ ] Bench, C3 + C2 for several hours, after #34: no mistiming loss; guard distribution and empty-window Rx time (before and after) reported.
- [ ] ADR: the guard strategy, the ratio, the thresholds; supersedes the "Version 2" section of ADR-0012.

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
