# Issue #34: runtime drift estimator and RTC smooth calibration on hardware

Purpose: acceptance. Issue: #34.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/runtime-calibration.toml` (to write once the estimator and the `CALR` write path are in) | C3 + C2 for several hours: the C2 reaches `CLOCK_WARM`, its `DRIFT` rate estimate converges, `CALR` is written on one-step changes only, and no Tier 3 or `SYNC_LOST` occurs |
| same scenario with the Build Override that disables the `CALR` write (control, name set by the implementation) | The uncalibrated rate of the same pair over the same duration (the 2026-09-26 bench gave +8.1 ppm), for comparison |

## Hardware

Checked against `bench boards` on 2026-10-02: no probe connected.

| Item | Needed |
|---|---|
| Node ? flashed as C3 | yes |
| Node ? flashed as C2 | yes |
| Host on AC power, Windows sleep off, for the several-hour runs | yes |

Hands on the bench: plugging the two boards in.
No CubeMX regeneration.

## Criteria

- [ ] The C2's rate estimate converges (`DRIFT` events), and `CALR` is written only when the estimate moves by a step (`CALR` events).
- [ ] The residual rate after calibration is measured and reported with its uncertainty, against the control stretch with calibration off. This figure is the input of #45.
- [ ] No Tier 3, `SYNC_LOST` or lost lines during the run; shift and re-anchor paths unchanged.
- [ ] The duration of the `HAL_RTCEx_SetSmoothCalib` call is measured with `timing-probe` and recorded here, before the calling context is chosen.

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
