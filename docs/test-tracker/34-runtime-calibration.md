# Issue #34: runtime drift estimator and RTC smooth calibration on hardware

Purpose: acceptance. Issue: #34.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/runtime-calibration.toml` | C3 (Node 1) + C2 (Node 4, detuned +7.6 ppm at boot) for 3.9 h, on the budget-compliant Sync schedule (below): the C2 reaches `CLOCK_WARM`, a Tier 2 shift is made and unwrapped, its `DRIFT` rate estimate becomes valid (`ok=1`) within 40 min, `CALR` is written (`res=ok`) within 45 min and never fails, the residual stays within one step (954 ppb) for 30 estimates after it, and no Tier 3, `SYNC_SILENCE` or `SLOT_SUSPECT` occurs |
| `tools/bench/scenarios/runtime-calibration-off.toml` | The control stretch: the same detuned pair and schedule for 2.1 h with the Build Override `BENCH_CALR_OFF=1` (the estimator and `DRIFT` still run, no `CALR` write): the residual stays at about 8.2 ppm (7000 to 9500 ppb for 20 estimates) and several Tier 2 shifts correct the clock |
| `tools/bench/scenarios/sync-budget-c3.toml` | One C3 (Node 1) on the schedule of the two runs above: a `SYNC_TX` in cell 0 of each 120 s phase, five of them (8.1 min, past the 6.1 min after which the default schedule is denied), no `TX_DENIED`, no `TX_LATE`. At most 10 min and 1 node, so it is part of the derived regression suite |
| `tools/bench/scenarios/calr-boot.toml` | One board (Node 4 as C2, no C3): the platform reads `RTC_CALR` at boot and logs `CALR res=boot`, with no `DRIFT` and no write. At most 10 min and 1 node, so it is part of the derived regression suite |

## Sync schedule of the runs: the duty-cycle budget

The default Sync table cannot be used for a multi-hour run.
It has two Sync phases of ten cells, a cell every 3 s, and C3 and a relaying C2 send in the first `SYNC_TX_BUDGET` = 3 cells of each phase: 6 packets of 991 ms in every 60 s frame, **9.9 % duty cycle against the band's 1 %**.
The compliance engine's credit is 36 s, refilled at 1 % of the elapsed time, so it is gone after 6.1 min; then a node sends one packet per 99 s and every other Sync slot is `TX_DENIED` (over 4 h: 179 of 1440 scheduled packets sent).
The bench traces agree: in more than ten sessions on 2026-09-29 and 2026-10-01 the first `TX_DENIED` came 6.1 to 6.6 min after boot (`Tests/unit/test_sync_budget.c`, built as `test_sync_budget_default`, computes it with the real compliance engine and table).

A starved C3 would have turned the 4 h run into a test of the duty-cycle limit, which the field will not see (#45 sets the product schedule).
Both runs therefore use two Build Overrides, defaults unchanged: `SYNC_TX_BUDGET=1u` and `SYNC_CELL_GAP_MS=9500u`.
They give a Sync phase every 120 s (ten cells of 12 s) with one packet per phase and node: 0.83 % duty cycle, the credit only ever fills (`test_sync_budget_run`: never denied in 4 h, for C3 and for a relaying C2).
`sync-budget-c3.toml` shows the schedule on a board, and both runs forbid `TX_DENIED`.
At one packet per 120 s the estimator is valid after about 20 min (11 packets over 1200 s), the window of 48 samples spans 96 min, and acquisition takes about three packets (6 min) instead of 1.5 min.

## Starting state of the runs: the register and the pair

`RTC_CALR` lives in the RTC and survives a re-flash and a reset of the board.
A run that must start from a known setting therefore says so, with the Build Override `BENCH_CALR_BOOT_PULSES=<N>`: the setting of N net pulses is written at boot and logged `CALR res=preset` (a normal build keeps what it reads, `res=boot`).
The first control attempt (2026-10-02 21:19) had no such override, inherited the -954 ppb setting the calibrated run had written 20 minutes before (its first `DRIFT` line read `resid=-2142` against `rate=-1188`) and was aborted: it was calibrated, not a control.

Node 1 (C3) and Node 4 (C2) are matched to **+0.59 ppm**: with the register at 0 the estimate settled at 574 to 602 ppb over 40 min (run of 21:42), under the 715 ppb residual at which a write is due, so no `CALR` was written, as designed.
The criteria need a rate to cancel, and the 2026-09-26 bench had +8.1 ppm between two other boards, so both runs detune the C2's RTC at boot through the same register with `BENCH_CALR_BOOT_PULSES=8` (+8 pulses, +7.63 ppm), as a wrong static calibration (#23) would.
The C2 then runs about +8.2 ppm fast against the C3.
The estimator knows the applied setting: its `rate` is the one with no calibration (about 0.6 ppm, the crystals' own offset) and its `resid` is `rate + applied`, about 8.2 ppm, which the write must bring under one step.
The control run keeps the detune and disables the write: its residual stays at about 8.2 ppm and the Tier 2 shift is its only correction.
The natural +0.59 ppm is a result in itself: this pair needs no calibration, and the rule leaves it alone.

## Hardware

Checked against `bench boards` on 2026-10-02: 2 boards connected, with known Node IDs.

| Item | Needed | Today |
|---|---|---|
| Node 1 flashed as C3, probe `003D003D3234510833353533`, COM8 | yes | connected |
| Node 4 flashed as C2, probe `004D00303333511431363730`, COM10 | yes | connected |
| Host on AC power, Windows sleep off, for the 3.9 h and 2.1 h runs | yes | per session; a sleep invalidates the run |

Hands on the bench: none while the two boards stay plugged in.
No CubeMX regeneration.

## Host level (done)

Logic is proven on the host, so the bench only has to show what the host cannot: the real rate of a real pair, the real `CALR` write, and the paths together.

- `Tests/unit/test_rtc_calr.c`: ppb to `CALP`/`CALM` and back, both signs, range edges, rounding, every setting round trip, the RM0453 formula.
- `Tests/unit/test_drift_estimator.c`: synthetic series at 8.1 ppm with 1 ms quantised noise (sigma 0.36 ms), shifts, `CALR` changes, `RTC_SET` breaks, outliers, a cold gap, counter wrap.
  Convergence over 2000 seeds: the first valid estimate (after 20 min of packets) has an rms error of 0.18 ppm and a worst case of 0.72 ppm, under one step (0.954 ppm); with a full window the rms error is 0.077 ppm. The bench's 13 packets over 570 s give 0.56 ppm, which is why they are not enough.
- `Tests/unit/test_sync_budget.c`: the Sync schedule against the 1 % duty cycle, the default table (denied after 6.1 min) and the overrides of the runs (never denied in 4 h).
- `Tests/unit/test_probe.c`: the timing probe helper, with `BENCH_PROBE` and without.
- `Tests/unit/test_calr_policy.c`: write at 3/4 of a step, no dithering around the middle of two steps.
- `Tests/unit/test_mac_state_machine_c1.c` and `_c2.c`: the sample hook is called for good ACQUIRING and Tier 1 and 2 packets, never for Packet 1, a bad packet or Tier 3, and before the Tier 2 shift.
- No float on target: the build 2964ff8-d069b91 has no soft-float helper, and the new code costs 5006 bytes of CM0+ flash (128 KB) and 667 bytes of RAM (24 KB).

## Criteria

- [ ] The C2's rate estimate converges (`DRIFT` events), and `CALR` is written only when the residual reaches 3/4 of a step (`CALR` events).
- [ ] The residual rate after calibration is measured and reported with its uncertainty, against the control stretch with calibration off. This figure is the input of #45.
- [ ] No Tier 3, `SYNC_LOST` or lost lines during the run; shift and re-anchor paths unchanged.
- [x] The duration of the `HAL_RTCEx_SetSmoothCalib` call is measured with `timing-probe` and recorded here, before the calling context is chosen. (Measurement below. The write was put in the Sync packet context first, as the shift writes; the 52 us confirms the choice.)

The `HAL_RTCEx_SetSmoothCalib` call runs in the context of the Sync packet processing (the radio ISR), as the shift and set writes do, and is skipped while `RECALPF` is set; the duration criterion above checks that choice.

## Measurement: the `HAL_RTCEx_SetSmoothCalib` call

Probe `BENCH_CALR_PROBE` (temporary, never committed): at boot, three times, write `CALR` to -8 pulses and back to 0, timing the call and the time `RECALPF` stays set after it, with SysTick.
Node 4 as C2, `SystemCoreClock` = 4 000 000 Hz, build `969efc9-db7efdd-o494085`, run `tools/arclog/runs/20261002T195718Z-969efc9-db7efdd-o494085/`.

| Segment | Result (6 writes) |
|---|---|
| The call, `RECALPF` clear | 52 us every time |
| `RECALPF` set after the write | 88 to 113 us (median 101 us), about 3 RTCCLK cycles (92 us) |

The write is as cheap as a register write: in the radio ISR, next to the shift writes, it adds 52 us.
A second write less than about 0.1 ms after the first would find `RECALPF` set and be skipped; the writes are at least one Sync packet (30 s) apart, so it does not happen.
The probe ran in thread context at boot, with no sleep and no ISR in the timed span.

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
| 2026-10-02 19:54 | flags: `--node 4=C2 --expect "4 CALR res=boot" --forbid DRIFT` | `969efc9` | PASS | `tools/arclog/runs/20261002T195400Z-969efc9/` | One board, no C3. The boot path on the target: `CALR req=0 calp=0 calm=0 res=boot` right after `MAC_INIT`, the register held 0. Not an acceptance run: the criteria need the pair |
| 2026-10-02 19:57 | flags as above plus `-D BENCH_CALR_PROBE=1` | `969efc9-db7efdd-o494085` | measurement | `tools/arclog/runs/20261002T195718Z-969efc9-db7efdd-o494085/` | The timing probe of the section above; the board ends with `CALR` back to 0 |
| 2026-10-02 20:00 | flags: `--node 4=C2 --expect "4 CALR res=boot"` | `969efc9` | PASS | `tools/arclog/runs/20261002T200049Z-969efc9/` | After the probe: `CALR req=0 calp=0 calm=0 res=boot`, the board is back to no calibration |
| 2026-10-02 20:14 | `calr-boot.toml` | `eb30626` | PASS | `tools/arclog/runs/20261002T201429Z-eb30626/` | The saved scenario of the boot path: `CALR req=0 calp=0 calm=0 res=boot` on Node 4, no `DRIFT`, no write |
| 2026-10-02 20:31 | `sync-budget-c3.toml` (4 packets) | `19ee541-obae62b` | PASS | `tools/arclog/runs/20261002T203154Z-19ee541-obae62b/` | Node 1 as C3: `SYNC_TX ce=0` at 5.7, 125.2, 245.2, 365.2 s, 120.0 s apart. Ended at 365 s, before the 366 s where the default schedule is first denied: the scenario then asked for a 5th packet |
| 2026-10-02 20:39 | `sync-budget-c3.toml` | `19ee541-obae62b` | PASS | `tools/arclog/runs/20261002T203905Z-19ee541-obae62b/` | Five packets alternating `ph=0` and `ph=1`, 120 s apart, the last at 8.1 min; no `TX_DENIED`, no `TX_LATE` |
| 2026-10-02 20:51 | flags: `--node 4=C2 -D BENCH_PROBE=1 --expect "4 PROBE tag=drift_init"` | `8b0245d-od66516` | PASS | `tools/arclog/runs/20261002T205129Z-8b0245d-od66516/` | The probe helper on the target: `PROBE tag=drift_init seg=init_with_log us=4497 hz=4000000` (the boot `CALR` log line is in that span) |
| 2026-10-02 20:55 | `runtime-calibration.toml` (before it held the run for 4 h) | `8b0245d-obae62b` | PASS | `tools/arclog/runs/20261002T205533Z-8b0245d-obae62b/` | Staged checks only: it ended at 24 min, when its expectations were met. Node 4 reached ACQ at 127 s and WARM at 367 s; `DRIFT ok=1` at 1447 s (n=11, baseline 1200 s, rate 909 ppb, noise 311 us), then `CALR req=-909 calp=0 calm=1 res=ok` (-954 ppb, residual about -45 ppb). Rate series 556, 808, 909 ppb at n=9, 10, 11. `SYNC_RX` err 0 or 1 ms throughout, no `TX_DENIED`. The pair is only +0.9 ppm apart. Not the several-hour criterion: the scenario was then given the `DRIFT` count that holds it |
| 2026-10-02 21:19 | `runtime-calibration-off.toml` | `e74c5c9-o96ffe7` | invalid | `tools/arclog/runs/` (aborted at 21:37) | The register still held the setting of the calibrated run (see Starting state): not a control. Aborted by the agent, no verdict; run again with `BENCH_CALR_RESET=1` |
| 2026-10-02 21:42 | `runtime-calibration.toml` (natural pair, register at 0) | `f3ec6ae-o1569be` | FAIL | `tools/arclog/runs/20261002T214247Z-f3ec6ae-o1569be/` | `CALR res=ok` not within 45 min: no write was due. Estimate 321, 229, ..., 574 ppb (n=12 to 21), `noise` 280-324 us, `SYNC_RX` err 1 to 2 ms, no `TX_DENIED`: the pair is +0.59 ppm, under the 715 ppb threshold. Not a firmware defect; the runs now detune the C2 (see Starting state). The first 11 estimates read `rate=0 noise=0`: the errors were all the same 1 ms, a flat quantised line, which the 0.7 ms the pair gains in 20 min does not bend |
| 2026-10-02 22:28 | `runtime-calibration-off.toml` (natural pair) | `f3ec6ae-o96ffe7` | invalid | `tools/arclog/runs/` (aborted at 22:40) | Started automatically after the failed run; it would only have measured the natural 0.59 ppm. Aborted by the agent, no verdict |
| 2026-10-02 22:33 | `runtime-calibration-off.toml` | `329ce7e-o66055a` | PASS | `tools/arclog/runs/20261002T223324Z-329ce7e-o66055a/` | The control, 2.1 h (60 `DRIFT`). Node 4 detuned by `CALR res=preset req=7629`. Residual with no calibration: 8128 ppb mean, sd 34, 8035 to 8183 over the 32 valid estimates (7629 applied + the pair's own offset). Seven Tier 2 shifts (err 8 or 9 ms, 32 or 36 ticks) at 16 to 18 min intervals, the only correction. The estimator's own `rate` (no calibration, the 7 shifts unwrapped): 409 ppb at the first valid estimate (n=11), then 466 to 554 ppb, 530 at the end: the pair's offset again, as with the register at 0 (574 to 602 ppb), so the shifts did not bend the slope on the target. `noise` 246 to 306 us. No `TX_DENIED`, Tier 3 or silence |

