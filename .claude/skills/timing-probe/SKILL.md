---
name: timing-probe
description: Measure how long firmware code takes on the bench boards with a temporary cycle-counter probe. Use when a timing budget, a latency or a lost-time offset must be measured on hardware rather than estimated (a Tx lead, an RTC write, an ISR path, a HAL call).
---

# Timing probe

A **probe** is temporary firmware code that timestamps the boundaries of a code path and logs one trace line with the time of each segment.
It answers "how long does this take on the real chip", which no host test can: at the CM0+'s 4 MHz every HAL call costs hundreds of microseconds (#55: `HAL_RTC_SetTime` 672 us).
A probe is never committed; its numbers are, in the Test Record.

Build, flash and run only through `bench` (skill `bench`).

## Steps

1. **Probe.** In the file under test, wrap every probe line in `#ifdef BENCH_<TOPIC>_PROBE`.
   Use SysTick as a free-running 24-bit down-counter of CPU cycles, marks at each boundary, and one `ARCLOG` line after the timed path with each segment in microseconds and `SystemCoreClock`:

   ```c
   #ifdef BENCH_RTC_SET_PROBE
   static uint32_t probe_now(void) { return SysTick->VAL; }
   static uint32_t probe_us(uint32_t from, uint32_t to)       /* VAL counts down */
   {
       return ((from - to) & 0xFFFFFFu) / (SystemCoreClock / 1000000u);
   }
   #endif
   ...
   #ifdef BENCH_RTC_SET_PROBE
       SysTick->LOAD = 0xFFFFFFu;  SysTick->VAL = 0u;
       SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;   /* no TICKINT */
       uint32_t p0 = probe_now();
   #endif
       HAL_RTC_SetTime(...);
   #ifdef BENCH_RTC_SET_PROBE
       uint32_t p1 = probe_now();
       ARCLOG(ARCLOG_MOD_SYNC, VLEVEL_L, "RTC_PROBE", "settime=%u hz=%u",
              (unsigned)probe_us(p0, p1), (unsigned)SystemCoreClock);
   #endif
   ```

   Log after the timed path, never inside it: an `ARCLOG` call costs time of its own.
   Done when every probe line is inside the `#ifdef` and the build without the define is unchanged.
2. **Event.** Add the probe event to `tools/arclog/src/arclog/schema.py` for the run; an unknown event fails the run as a schema problem before it reaches the code under test.
3. **Run.** Turn the probe on with a Build Override: `[overrides] BENCH_<TOPIC>_PROBE = "1"` in a scenario, or `-D BENCH_<TOPIC>_PROBE=1` with the flag form (`--node`, `--expect`); a scenario file and `-D` do not combine.
   Expect the probe event itself (`--expect "2 RTC_PROBE within=3m"`) so the run passes once the line is in.
   Done when the probe line is in the record for every node that ran the path.
4. **Record.** Put the numbers in the Test Record (`docs/test-tracker/README.md`) under a Measurement section: each segment, the core clock, the Build ID and the run record path; the run gets its row (verdict `measurement`).
   Count the use in `CLAUDE.md` (Timing measurements), and make the proposal it names when the count reaches 3.
5. **Remove.** `git checkout` the probed firmware files and the schema, then check that `git diff` holds no `PROBE`.
   Done when the probe is gone from the working tree, before any fix is built.

## Clock facts

- CM0+: `SystemCoreClock` = 4 MHz (measured 2026-10-01), so 4 cycles per microsecond and a SysTick wrap every 4.19 s; segments longer than that need the RTC instead.
- Log `SystemCoreClock` in every probe line rather than assuming it: the CM4 and other configurations may differ.
- SysTick is HAL's 1 ms tick on the CM0+ (`HAL_InitTick` default, `HAL_IncTick` in `SysTick_Handler`). The probe reconfigures it, which is harmless in a probe build only because `HAL_GetTick` comes from the RTC; never keep it in a committed build.
- SysTick does not run in STOP2: it counts the core clock, which is off there, and the firmware also suspends the tick before entering STOP2 (`stm32_lpm_if.c`). It resumes from the value it stopped at, so a segment that contains a sleep reads short by the whole sleep, with no sign of it. Before probing a path, check whether it can sleep (a sequencer idle, a `UTIL_TIMER` wait, an alarm); if it can, time it with the RTC, which runs on the LSE in STOP2: `TIMER_IF_GetDayTicks(true)` reads the time of day exactly on a 1/4096 s tick edge (244 us resolution, waits at most one tick). The RTC is also the choice for spans over a few seconds.
- A debugger can keep the core clock on in STOP2 (DBGMCU low-power debug bits), and then SysTick keeps counting: a probe gives different numbers with and without a debug session, so measure without one (bench never attaches a debugger to a run).
- SysTick counts wall time, interrupts included: a segment that an ISR preempted is longer by the ISR. Repeat the run when a number looks off.
