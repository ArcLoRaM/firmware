# Issue #45: Sync schedule within the duty-cycle budget, on hardware

Purpose: acceptance. Issue: #45.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/sync-schedule.toml` (to write once the schedule is chosen) | C3 + C2 for several hours: no `TX_DENIED` on Sync slots after the boot burst, and the C2's `SYNC_RX` rate equals the schedule's |

## Hardware

Checked against `bench boards` on 2026-10-02: no probe connected.

| Item | Needed |
|---|---|
| Node ? flashed as C3 | yes |
| Node ? flashed as C2 | yes |
| Host on AC power, Windows sleep off, for the several-hour run | yes |

Hands on the bench: plugging the two boards in.
No CubeMX regeneration.
The worst-case relay airtime of a 4-hop line is a host computation here, checked on hardware in the #76 record.

## Criteria

- [ ] The residual drift rate from #34 and the Sync period derived from it, in the ADR.
- [ ] A documented budget: Sync airtime per hour per node class (C3, the worst relaying C2) at most the band's duty cycle, with margin.
- [ ] Host: the table lint of #56 passes for each class.
- [ ] Bench, multi-hour C3 + C2: no `TX_DENIED` on Sync slots after the boot burst.
- [ ] Every scheduled Sync packet is received by the C2 (except genuine radio loss), so its `SYNC_RX` rate equals the schedule's.
- [ ] ADR for the chosen schedule and period.

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
