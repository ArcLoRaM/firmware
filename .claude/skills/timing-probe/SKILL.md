---
name: timing-probe
description: Measure how long firmware code takes on the bench boards with the PROBE_START/MARK/LOG helper (compiled in only by the BENCH_PROBE Build Override). Use when a timing budget, a latency or a lost-time offset must be measured on hardware rather than estimated (a Tx lead, an RTC write, an ISR path, a HAL call).
---

# Timing probe

A **probe** is firmware code that timestamps the boundaries of a code path and logs one trace line (`PROBE`) with the time of each segment.
It answers "how long does this take on the real chip", which no host test can: at the CM0+'s 4 MHz every HAL call costs hundreds of microseconds (#55: `HAL_RTC_SetTime` 672 us).
The helper is `Common/Log/probe.h`: three macros that compile to nothing unless the Build Override `BENCH_PROBE` is set, so a probe can stay in the source without costing a byte in a production image.
Its numbers are kept in the Test Record.

Build, flash and run only through `bench` (skill `bench`).

## Steps

1. **Probe.** Include `probe.h` in the file under test and wrap the path:

   ```c
   #include "probe.h"
   ...
   PROBE_START(p);
   HAL_RTC_SetTime(...);
   PROBE_MARK(p, "settime");
   HAL_RTC_SetDate(...);
   PROBE_MARK(p, "setdate");
   PROBE_LOG(p, "rtc_set");     /* after the timed path: an ARCLOG call costs time of its own */
   ```

   One `PROBE tag=rtc_set seg=settime us=672 hz=4000000` line per mark.
   Tag and labels are string literals without spaces; at most 8 marks per probe.
   Done when the build without `BENCH_PROBE` is unchanged (the macros are empty statements).
2. **Run.** Turn the probes on with a Build Override: `[overrides] BENCH_PROBE = "1"` in a scenario, or `-D BENCH_PROBE=1` with the flag form (`--node`, `--expect`); a scenario file and `-D` do not combine.
   Expect the probe event itself (`--expect "2 PROBE tag=rtc_set within=3m"`) so the run passes once the line is in.
   Done when the probe line is in the record for every node that ran the path.
3. **Record.** Put the numbers in the Test Record (`docs/test-tracker/README.md`) under a Measurement section: each segment, the core clock, the Build ID and the run record path; the run gets its row (verdict `measurement`).
4. **Keep or remove.** A probe on a path that stays interesting (a write, an ISR) can stay in the source; remove a probe that was only a one-off question.
   No schema edit and no clean-up are needed either way: `PROBE` is a permanent event.

## Clock facts

- CM0+: `SystemCoreClock` = 4 MHz (measured 2026-10-01), so 4 cycles per microsecond and a SysTick wrap every 4.19 s; segments longer than that need the RTC instead.
- Log `SystemCoreClock` in every probe line rather than assuming it: the CM4 and other configurations may differ.
- SysTick is HAL's 1 ms tick on the CM0+ (`HAL_InitTick` default, `HAL_IncTick` in `SysTick_Handler`). The probe reconfigures it, which is harmless in a `BENCH_PROBE` build only because `HAL_GetTick` comes from the RTC; that is why the helper exists only under that Build Override.
- SysTick does not run in STOP2: it counts the core clock, which is off there, and the firmware also suspends the tick before entering STOP2 (`stm32_lpm_if.c`). It resumes from the value it stopped at, so a segment that contains a sleep reads short by the whole sleep, with no sign of it. Before probing a path, check whether it can sleep (a sequencer idle, a `UTIL_TIMER` wait, an alarm); if it can, time it with the RTC, which runs on the LSE in STOP2: `TIMER_IF_GetDayTicks(true)` reads the time of day exactly on a 1/4096 s tick edge (244 us resolution, waits at most one tick). The RTC is also the choice for spans over a few seconds.
- A debugger can keep the core clock on in STOP2 (DBGMCU low-power debug bits), and then SysTick keeps counting: a probe gives different numbers with and without a debug session, so measure without one (bench never attaches a debugger to a run).
- SysTick counts wall time, interrupts included: a segment that an ISR preempted is longer by the ISR. Repeat the run when a number looks off.
