# NUCLEO pair, 12 h, production behaviour: Sync reception, rate stability, trim

Purpose: performance. Issue: none; the figures feed #45 (the Sync schedule), #36 (the guard from the drift estimate) and the Phase 2 issues (#41, #43).

The clock is the NUCLEO Clock (NDK NX3215SA crystal, CONTEXT.md): every figure is a NUCLEO result and does not transfer to the Production Clock (SiT1552 TCXO, +-5 ppm).

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/pair-drift-12h.toml` | C3 (Node 1) + C2 (Node 4) for 12 h with nothing injected: the runtime calibration enabled, the baseline cleared at boot (`BENCH_CALR_BOOT_PULSES=0`), a Sync phase every 120 s inside the duty-cycle budget (`SYNC_TX_BUDGET=1u`, `SYNC_CELL_GAP_MS=9500u`). Held by 350 `DRIFT` lines (11.7 h). Forbids `TX_DENIED`, Tier 3, `SYNC_SILENCE`, `SLOT_SUSPECT`, a failed `CALR` write |

## Hardware

Checked against `bench boards` on 2026-10-03: 2 boards connected, with known Node IDs.

| Item | Needed | Today |
|---|---|---|
| Node 1 flashed as C3, probe `003D003D3234510833353533`, COM8 | yes | connected |
| Node 4 flashed as C2, probe `004D00303333511431363730`, COM10 | yes | connected |
| Host on AC power for 13 h, Windows sleep off (on AC the standby timeout is 0, on battery it is 15 min), lid action not to sleep | yes | to check by the user |

Hands on the bench: none during the run.
No CubeMX regeneration.

## Criteria

The figures of a performance record are reported as measured, with their counts; no threshold is set after the data is seen.

- [ ] Run executed over 11.7 h or more and stored; an invalid run (a host sleep, a dead radio) stays in Runs with its cause.
- [ ] Sync reception ratio: `SYNC_RX` at the C2 over `SYNC_TX` at the C3 for the 12 h, with both counts, and each miss with its cause (the C2's `RX_TIMEOUT` or `RX_ERROR` around it).
- [ ] The duty-cycle budget holds on air for 12 h: no `TX_DENIED` on either node.
- [ ] The pair's rate against time: the estimator's `rate` per hour (mean, spread, largest change), and the raw `SYNC_RX` error slope between shifts, over the 12 h.
- [ ] The runtime trim: the number of `CALR` writes and when, the `trim` and `resid` range; the number of Tier 2 shifts.
- [ ] The estimator's `noise` over the run and the `SYNC_RX` error scatter (the input of the guard, #36).

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
