# ADR-0009: RTC Prescaler — PREDIV_A = 7, PREDIV_S = 4095

## Status
Accepted

## Context
The RTC runs in `RTC_BINARY_NONE` mode (BCD calendar + SubSeconds register) on a
32 768 Hz LSE. The TDMA sync algorithm corrects the Frame Epoch using sub-millisecond
preamble offset measurements — the SubSeconds register is the time source for these
corrections. The prescaler pair must satisfy two constraints simultaneously:

1. `(PREDIV_A + 1) × (PREDIV_S + 1) = 32 768` — required for a valid 1 Hz calendar tick
2. Sub-millisecond SubSeconds resolution — required to make meaningful sync corrections

ST's power-optimal recommendation (AN4759 §2.14.3) is `PREDIV_A = 127`, `PREDIV_S = 255`,
giving ck_apre = 256 Hz and 3.9 ms subsecond resolution. That resolution is too coarse:
a 3.9 ms correction step cannot support sub-millisecond TDMA guard times.

The opposite extreme (`PREDIV_A = 0`, `PREDIV_S = 32767`, 30.5 µs resolution) satisfies
the resolution requirement but drives ck_apre to the full 32 768 Hz LSE rate — the worst
possible power configuration for a battery node spending most of its life in Stop2.

The previous codebase used a formula-coupled approach (`RTC_N_PREDIV_S` as a single
exponent deriving both prescalers). That pattern forces `PREDIV_A = 0` whenever fine
resolution is needed (N = 15 → PREDIV_A = 0), and `PREDIV_A = 127` only when resolution
is coarse (N = 8 → PREDIV_S = 255). It cannot express the middle-ground independently.

## Decision
`PREDIV_A = 7`, `PREDIV_S = 4095` as flat, decoupled constants.

- `(7 + 1) × (4095 + 1) = 8 × 4096 = 32 768` → ck_spre = 1 Hz ✓
- ck_apre = 32 768 / 8 = 4 096 Hz — acceptable power, far below the 32 768 Hz worst case
- Subsecond resolution = 1 / 4096 s ≈ **244 µs** — one SSR tick, sub-millisecond ✓


244 µs is the finest sync correction granularity via `HAL_RTCEx_SetSynchroShift`, which
is sufficient for the sub-millisecond Frame Epoch corrections needed in TDMA operation.

## Consequences
All subsecond arithmetic must use `PREDIV_S = 4095` literally or via the `RTC_PREDIV_S`
macro. The formula `(PREDIV_A+1) × (PREDIV_S+1) = 32768` must be verified manually
if either constant is ever changed — there is no longer a compile-time algebraic coupling
to enforce it.
