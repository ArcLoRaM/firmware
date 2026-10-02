# Issue #34: runtime drift estimator and RTC smooth calibration on hardware

Purpose: acceptance. Issue: #34.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/runtime-calibration.toml` | C3 + C2 for 4 h: the C2 reaches `CLOCK_WARM`, its `DRIFT` rate estimate becomes valid (`ok=1`) within 40 min, `CALR` is written (`res=ok`) within 45 min and never fails, and no Tier 3, `SYNC_SILENCE` or `SLOT_SUSPECT` occurs |
| `tools/bench/scenarios/runtime-calibration-off.toml` | The control stretch: the same pair for the same 4 h with the Build Override `BENCH_CALR_OFF=1` (the estimator and `DRIFT` still run, no `CALR` write): the uncalibrated rate of the pair (the 2026-09-26 bench gave +8.1 ppm) |

The first scenario assumes a pair whose rate offset is above 0.72 ppm, the residual at which a write is due; on a closer pair no `CALR` is written, and its `CALR` expectation fails with that cause (read it from the `DRIFT` rate).

## Hardware

Checked against `bench boards` on 2026-10-02: 1 board connected, probe `004D00303333511431363730` on COM10, Node ID unknown (no `BOOT` in the capture yet).

| Item | Needed | Today |
|---|---|---|
| Node ? flashed as C3 | yes | the connected board, once its Node ID is known |
| Node ? flashed as C2 | yes | missing: a second board |
| Host on AC power, Windows sleep off, for the 4 h runs (twice) | yes | per session |

Hands on the bench: plugging the second board in.
No CubeMX regeneration.

## Host level (done)

Logic is proven on the host, so the bench only has to show what the host cannot: the real rate of a real pair, the real `CALR` write, and the paths together.

- `Tests/unit/test_rtc_calr.c`: ppb to `CALP`/`CALM` and back, both signs, range edges, rounding, every setting round trip, the RM0453 formula.
- `Tests/unit/test_drift_estimator.c`: synthetic series at 8.1 ppm with 1 ms quantised noise (sigma 0.36 ms), shifts, `CALR` changes, `RTC_SET` breaks, outliers, a cold gap, counter wrap.
  Convergence over 2000 seeds: the first valid estimate (after 20 min of packets) has an rms error of 0.18 ppm and a worst case of 0.72 ppm, under one step (0.954 ppm); with a full window the rms error is 0.077 ppm. The bench's 13 packets over 570 s give 0.56 ppm, which is why they are not enough.
- `Tests/unit/test_calr_policy.c`: write at 3/4 of a step, no dithering around the middle of two steps.
- `Tests/unit/test_mac_state_machine_c1.c` and `_c2.c`: the sample hook is called for good ACQUIRING and Tier 1 and 2 packets, never for Packet 1, a bad packet or Tier 3, and before the Tier 2 shift.
- No float on target: the build 2964ff8-d069b91 has no soft-float helper, and the new code costs 5006 bytes of CM0+ flash (128 KB) and 667 bytes of RAM (24 KB).

## Criteria

- [ ] The C2's rate estimate converges (`DRIFT` events), and `CALR` is written only when the estimate moves by a step (`CALR` events).
- [ ] The residual rate after calibration is measured and reported with its uncertainty, against the control stretch with calibration off. This figure is the input of #45.
- [ ] No Tier 3, `SYNC_LOST` or lost lines during the run; shift and re-anchor paths unchanged.
- [ ] The duration of the `HAL_RTCEx_SetSmoothCalib` call is measured with `timing-probe` and recorded here, before the calling context is chosen.

The `HAL_RTCEx_SetSmoothCalib` call runs in the context of the Sync packet processing (the radio ISR), as the shift and set writes do, and is skipped while `RECALPF` is set; the duration criterion above checks that choice.

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
