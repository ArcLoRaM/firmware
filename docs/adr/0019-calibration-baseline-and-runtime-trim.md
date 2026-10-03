# ADR-0019: Calibration baseline and runtime trim

## Status
Accepted for the principle. The numeric trim windows, the trim's rate limit and the baseline procedure are open (see Open decisions).

## Context
ADR-0018 calibrates a node's RTC to the timeline of its Sync sender: the estimator measures the rate against the sender and the write is the opposite of it.
The bench of 2026-10-02 and 2026-10-03 showed the loop works against an ideal sender: a detuned C2 went from +8.118 ppm to -0.362 ppm against the C3.
It showed nothing about a sender that is itself off, and in a mesh the sender of a node is often a relaying C2:

- a node adopts its sender's error as well as cancelling its own, so a C2's temperature wander is followed by the nodes below it;
- the errors add along the chain, and two nodes at the same depth on different paths can end up with different calibrations, which breaks the concurrent transmission budget (`2 x depth x per-hop error < 16 ms`);
- nothing bounds the write except the register range of -487 to +488 ppm: a sender wrong by 50 ppm would pull its neighbours by 50 ppm.

## Decision
The calibration matches an **ideal clock**, not the clock next to it.
Two mechanisms, applied together in the one `RTC_CALR` register: `applied = baseline + trim`.

- **Baseline calibration.** The setting of each board measured once against an ideal reference (the factory, #23, stored as a boot constant, #22) and loaded at boot.
  It gives every board the same absolute footing, whatever the sender does.
  Until the factory path exists the baseline is the register as read at boot, and a bench run that needs another one writes it with `BENCH_CALR_BOOT_PULSES`.
- **Runtime trim.** What the estimator adds on top: it follows what changes after the baseline (temperature, ageing, supply) and the baseline's own error.
  It is not meant to follow a neighbour.
- **The C3 is the ideal clock, all the time.** A baseline-calibrated C3, disciplined to an external time (#47, #35) once that exists, is the common reference of the mesh.
- **The trim is bounded to a window around the baseline, and the window belongs to the clock type.**
  It is a per-board compile-time constant, like the node class, not a protocol constant.
  An estimate outside the window is not followed: it is logged as an anomaly (a suspect sender or a suspect node).
- **No limit on how fast the trim moves.** The arctic can change the temperature quickly, and a rate limit would make the node lag exactly when it is needed.
  To be confirmed experimentally.
- **The log carries the split.** `CALR` and `DRIFT` events have `cal0` (the baseline, ppb, constant for a boot) and `trim` (applied minus baseline), so a replay and Phase 2 see the two apart.

The mechanism is the same for the two clocks of the project, defined in CONTEXT.md (Production Clock, NUCLEO Clock).
Only the window differs, and how much the estimator has to do. A statement that does not name its clock means the Production Clock.

| | NUCLEO Clock (development board): NDK NX3215SA crystal | Production Clock: SiTime SiT1552 TCXO, grade E |
|---|---|---|
| Tolerance | +-20 ppm at 25 C (not including ageing) | +-5 ppm over -40 to +85 C (+-10 ppm with the initial offset, which the baseline removes) |
| Temperature behaviour | parabola, turnover 20 to 30 C, -0.04 ppm/C^2 (maximum) | compensated inside the device, no parabola |
| Over -40 to +85 C | up to about 200 ppm slow at -40 C, 170 ppm at +85 C | within +-5 ppm |
| Rate of change of the rate | 2 ppm/C at 0 C, up to 5.6 ppm/C at -40 C | within the +-5 ppm |
| Ageing | not in the datasheet (a few ppm a year is typical for a tuning fork) | +-1 ppm the first year |
| Supply | not specified | +-1.5 ppm over 1.5 to 3.63 V |
| Where it is used | the lab, 15 to 35 C: 1 to 9 ppm from the turnover | the arctic, -40 to +85 C |
| Trim window | a few ppm in the lab; about 200 ppm if the board were taken to the cold | +-5 ppm for the temperature, plus 1 ppm a year of ageing |

Sources: NDK NX3215SA specification NDKX01-00001 (Table 1, items 4 to 7), SiTime SiT1552 datasheet rev 1.43 (Table 1).
A quartz crystal over an arctic temperature range needs a trim far above 5 ppm; the Production Clock does not, which is why a window of about 5 ppm is right for it.

The runtime layer is kept on the Production Clock.
What it has to follow there is small and slow: the baseline's own error, the ageing, the supply, and, in the mesh, a sender that is off (the reason for the window).

## Considered Options

- **Follow the sender, unbounded (ADR-0018 as built).** Works against an ideal sender and in a lab; rejected as the product behaviour for the reasons above.
- **A factory calibration only.** Cannot follow temperature or ageing, and the crystal of the development board varies by up to 200 ppm over temperature.
- **Runtime only, bounded around the register as read.** What this phase is until a factory value exists: the bound has no absolute meaning without a baseline measured against an ideal clock.

## Consequences

- The Phase 1 code (ADR-0018) still follows the sender and has no window: it is unbounded until the windows below are chosen. This ADR names the gap; it does not close it.
- Phase 2 (#41 temperature, #43 Kalman) was meant to model the temperature. On the Production Clock the temperature is compensated in the device, so a temperature model and a Kalman filter on it add little there: the estimator's job is the baseline error and the ageing. The temperature model matters for the NUCLEO Clock, a development tool. The scope of #41 and #43 should follow this (open decision below).
- Every bench result of #34 and of the long runs that follow is a NUCLEO Clock result: the figures (0.36 ppm residual, 0.59 ppm pair offset, +8.1 ppm detune) do not transfer to the Production Clock, which has to be measured on its own hardware.
- The bench runs of #34 used a preset as a wrong baseline: in them `trim` reads -8.6 ppm (the trim cancelling the detune), which is the intended use of the split, not an anomaly.

## Open decisions

- The numeric trim window of the NUCLEO Clock (the Production Clock's is +-5 ppm plus ageing; the register holds -487 to +488 ppm).
- Whether the trim has a rate limit: none for now, to be confirmed experimentally in the cold.
- What the node does with an estimate outside the window: saturate at the edge, or ignore it; and how it flags a suspect sender.
- The scope of #41 (temperature measurement) and #43 (Kalman estimator) now that the Production Clock is temperature compensated.
- The baseline measurement procedure and its ideal reference (#23, #22), and the disciplined C3 (#47).
