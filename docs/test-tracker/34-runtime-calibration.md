# Issue #34: runtime drift estimator and RTC smooth calibration on hardware

Purpose: acceptance. Issue: #34.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/runtime-calibration.toml` | C3 (Node 1) + C2 (Node 4) for 4 h, on the budget-compliant Sync schedule (below): the C2 reaches `CLOCK_WARM`, its `DRIFT` rate estimate becomes valid (`ok=1`) within 40 min, `CALR` is written (`res=ok`) within 45 min and never fails, and no Tier 3, `SYNC_SILENCE` or `SLOT_SUSPECT` occurs |
| `tools/bench/scenarios/runtime-calibration-off.toml` | The control stretch: the same pair, the same schedule, the same 4 h with the Build Override `BENCH_CALR_OFF=1` (the estimator and `DRIFT` still run, no `CALR` write): the uncalibrated rate of the pair (the 2026-09-26 bench gave +8.1 ppm) |
| `tools/bench/scenarios/sync-budget-c3.toml` | One C3 (Node 1) on the schedule of the two runs above: a `SYNC_TX` in cell 0 of each 120 s phase, five of them (8.1 min, past the 6.1 min after which the default schedule is denied), no `TX_DENIED`, no `TX_LATE`. At most 10 min and 1 node, so it is part of the derived regression suite |
| `tools/bench/scenarios/calr-boot.toml` | One board (Node 4 as C2, no C3): the platform reads `RTC_CALR` at boot and logs `CALR res=boot`, with no `DRIFT` and no write. At most 10 min and 1 node, so it is part of the derived regression suite |

The first scenario assumes a pair whose rate offset is above 0.72 ppm, the residual at which a write is due; on a closer pair no `CALR` is written, and its `CALR` expectation fails with that cause (read it from the `DRIFT` rate).

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

## Hardware

Checked against `bench boards` on 2026-10-02: 2 boards connected, with known Node IDs.

| Item | Needed | Today |
|---|---|---|
| Node 1 flashed as C3, probe `003D003D3234510833353533`, COM8 | yes | connected |
| Node 4 flashed as C2, probe `004D00303333511431363730`, COM10 | yes | connected |
| Host on AC power, Windows sleep off, for the 4 h runs (twice) | yes | per session; a sleep invalidates the run |

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
