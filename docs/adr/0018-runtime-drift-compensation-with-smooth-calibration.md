# ADR-0018: Runtime drift compensation with the RTC smooth calibration

## Status
Accepted

## Context
Until now the Sync corrections acted on the phase only: Tier 2 shifts the clock, Tier 3 and Packet 1 set it.
The crystal's rate was never measured, so the error came back after every correction.
The C3-C2 bench of 2026-09-26 measured a C2 running +8.1 ppm fast against the C3: Tier 1 (8 ms) is left after about 16 minutes without a packet, and the guard (100 ms) after about 3.5 hours.
The RTC has a hardware rate correction, the smooth calibration (`RTC_CALR`), that keeps working in Stop2.
It needs a rate to cancel, and only the Sync errors can give one at runtime.

## Decision
C1 and C2 estimate the **Rate Offset** of their own RTC against their Sync sender from the Sync errors alone, and cancel it with the **Smooth Calibration**.
No temperature and no Kalman filter in this phase: they are Phase 2 (#41, #43) and replace the estimator behind the same interface.

- **Estimator.** A least squares line over the latest 48 samples of the unwrapped Sync errors, not over consecutive deltas: with 1 ms stamps one 30 s pair only bounds the rate to 33 ppm, while 20 minutes of packets give about 0.2 ppm.
  Unwrapping removes what the node did to its own clock: each phase shift, and the applied calibration integrated over time, so the fitted slope is the rate with no calibration whatever was applied meanwhile.
  An RTC set breaks the segment (a new intercept, the slope shared), errors at or beyond the resync threshold are not samples, and outliers are rejected against the fit.
  It is pure integer C with no HAL (`drift_estimator.c`), and its estimate is valid only after a minimum baseline and sample count taken from the bench figures.
- **Actuation.** `RTC_CALR` with the 32 s window, the finest step (0.954 ppm) over -487.1 to +488.5 ppm.
  A new setting is written when the estimate is valid and the residual, rate plus applied calibration, reaches three quarters of a step.
  A threshold under half a step would let noise flip the setting back and forth, and a full step would leave up to 0.95 ppm of residual on a drifting crystal.
  A recalibration still pending (`RECALPF`) is skipped, not waited for: the next sample tries again.
- **C3 does not calibrate.** It is the time reference; calibrating it against an external reference is #47.
  A relaying C2 calibrates against its own sender, so the nodes below it inherit a steadier clock.
- **At boot** the estimator starts from the `RTC_CALR` held in the register, so a static calibration (#23) is the starting point and the runtime estimate adjusts what is left.
  The estimate itself is not persisted across a reboot: that is Phase 2.
- **The tiers keep working.** Shifts and re-anchors are unchanged. The calibration only makes them rarer.

## Considered Options

- **Consecutive deltas.** Rejected: the quantisation of a 1 ms stamp bounds the rate to 33 ppm over 30 s.
- **A static calibration only (#23).** Measured once per board, it cannot follow temperature, ageing or the sender's own rate. It stays as the starting value and the fallback if the estimator is switched off.
- **A software rate correction in the time base.** Rejected: the RTC is the only clock that runs in Stop2, so a correction applied in software would be lost whenever the core sleeps.
- **The 8 s and 16 s windows.** Coarser steps for no gain here.

## Consequences

- The residual left after calibration is what the guard (#36) and the Sync period (#45) must absorb: it is measured on the bench, with a control stretch with the write disabled (Build Override `BENCH_CALR_OFF=1`), in the Test Record of #34.
- The estimate is valid after 20 minutes of packets (40 at the Sync phase period of 30 s, 11 at the 120 s of the calibration runs), and before that no CALR is written.
- The default Sync table is 9.9 % duty cycle against the band's 1 %: the compliance credit is gone after 6.1 min and C3 and C2 are denied from then on. The calibration runs therefore use a schedule inside the budget (Build Overrides `SYNC_TX_BUDGET=1u`, `SYNC_CELL_GAP_MS=9500u`: a Sync phase every 120 s, 0.83 %), so that the figure measured is the field's, not a starved link's. The product schedule is #45's.
- The window holds a number of samples, not a duration: at a long Sync period it spans a long time, and an age limit will be needed once #45 fixes the period.
- The CALR write runs in the context of the Sync packet processing, as the shift and set writes do, and is subject to the same write-protect rule (#75).
- On the CM0+ the module and its platform code cost about 5 KB of flash and 670 bytes of RAM (bench build 2964ff8-d069b91), with no floating point.
