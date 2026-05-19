# ADR-0007: CM0+ Is the Sole RTC Writer

## Status
Accepted

## Context
Both CM4 and CM0+ need to program the RTC peripheral: CM0+ for Alarm A (TDMA
slot boundaries) and sync calibration (HAL_RTC_SetTime / HAL_RTC_SetDate); CM4
for Alarm B (sensor acquisition schedule). The RTC write-protection unlock
sequence is global — it covers all write-protected registers simultaneously,
creating a concurrent access hazard.

Two options were considered:

- **Option A (HSEM)**: Each core programs its own alarm. A Hardware Semaphore
  serialises access to the RTC write-protection sequence. Correct, but adds the
  HSEM peripheral as a dependency and requires both cores to follow the acquire/
  release protocol consistently.

- **Option B (single writer)**: CM0+ is the sole RTC writer. CM4, at the end of
  each sensor window, writes its desired next Alarm B time to a shared memory
  structure (`AlarmBRequest`). CM0+ reads this on its next wakeup and programs
  Alarm B before returning to sleep. No locking primitive required.

## Decision
Option B. CM0+ already wakes at every TDMA slot boundary — far more frequently
than CM4 needs a new Alarm B. CM0+ will always read and program the request well
before CM4 needs it. This eliminates the concurrent access problem entirely
without introducing a locking primitive.

## Consequences
CM4 no longer programs its own wakeup directly. It trusts CM0+ to relay the
Alarm B write via shared memory. In the pathological case where CM0+ misses
reading the request before the desired wake time (should not occur in normal
operation), CM4 sleeps one sensor window longer than intended — one missed
acquisition, acceptable by design. CM0+ is the single source of truth for all
RTC state.
