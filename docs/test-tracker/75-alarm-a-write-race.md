# Issue #75: can an ISR RTC write drop an Alarm A write?

Purpose: acceptance. Issue: #75.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| none (flags form, `-D BENCH_ALARM_RACE_PROBE=1`, probe not committed) | An ISR-side RTC write injected at every microsecond of `plat_program_alarm_a` and `plat_cancel_alarm_a` leaves the alarm registers as an uninterrupted call would |

The probe is a temporary patch (SysTick one-shot as the injected ISR, `rtc_write_calendar` as its body, alarm registers read back after each call), never committed.
It needs a `PROBE_ALARM` event in the arclog schema for the run.

## Hardware

| Item | Needed |
|---|---|
| Node 4 flashed as C2, probe `004D00303333511431363730`, COM10 | yes (one board is enough: the race is one core's thread against its own ISR) |

Node 4 is a new board (UID `003400443232501420383543`), registered in `Common/Protocol/node_id.c` for this work.
Hands on the bench: none.

## Criteria

- [ ] Written down in the issue (or an ADR if it changes a decision): whether the ISR can preempt an alarm write in practice, with the call paths.
  Measured below: the ISR does preempt the write, and it does not drop it, because the Alarm A registers take writes with write protection on.
- [ ] Alarm programming and cancelling, and every RTC write path, are mutually exclusive.
  Not done: held until the result below is reviewed (it shows no drop to prevent for Alarm A).
- [ ] Host test for the calendar write and Tier 2 shift between two slot wakes.
  Not done: a stubbed platform cannot see the write-protect race (issue body).
- [ ] Comment at the RTC write path stating that the CM4 LPTIM is LSI-clocked and unaffected by RTC writes.
  Not done.

## Measurement (2026-10-02, `BENCH_ALARM_RACE_PROBE`, not committed)

CM0+ at 4 MHz (`hz=4000000`), PRIMASK 0, C2 cold (no alarm chain), build `a52e900` plus the probe.

**Window and landings.**
A SysTick one-shot fires N us into the call (N = 1, 2, 3 ... us) and its handler runs the production ISR-side writer `rtc_write_calendar`.
`pre`, `mid` and `post` say where the ISR landed, from the alarm registers it saw: before the `ALRMAR` write, between that write and the final enable, after the enable.

| Call | Length | ISR landed inside | pre / mid / post | Dropped writes |
|---|---|---|---|---|
| `plat_program_alarm_a` | 283 us | 282 of 282 us steps | 255 / 10 / 17 | 0 |
| `plat_cancel_alarm_a` | 45 us | 44 of 44 us steps | 23 / 0 / 21 | 0 |

The stretch of the program call where `ALRMAR` is written but the alarm is not yet enabled is about 10 us (the `mid` count at 1 us steps); the rest is BCD conversion, the HAL's lock and the unlock before it.
After every injection the state (`ALRMAR`, `ALRMASSR`, `CR.ALRAE`, `CR.ALRAIE`) equals the uninterrupted call's.

**Why nothing is dropped: the Alarm A registers are not protected.**
With the write-protection relocked (`RTC->WPR = 0xFF`), a plain write to each register, read back (1 = the write landed):

| Register | Landed |
|---|---|
| `ALRMAR`, `ALRMASSR`, `ALRABINR`, `CR.ALRAE` | 1 |
| `WUTR` (wake-up timer, tested only while `ICSR.WUTWF` was set) | 1 |
| `ALRMBR`, `ALRMBSSR`, `ALRBBINR`, `CR.ALRBE` | 0 |
| `ICSR.INIT` | 0 (`INITF` never set) |

`ALRMAR` also took writes after a wrong key (`WPR = 0x00`).
A bare relock injected in the ISR instead of the calendar write (positive control, `BENCH_ALARM_RACE_PROBE=2`) drops nothing either, for the same reason.
The alarm B and `INIT` rows show the write protection itself is on and working on this board.

**Limits of this result.**

- One board (Node 4), one firmware state (C2, cold, no chain).
- Why Alarm A and `WUTR` are writable under the lock was not looked up in RM0453 or the errata: it is an observation.
- The wake-up timer was checked only through `WUTR`; `CR.WUTE`, `CR.WUCKSEL` and the HAL's other wake-up timer writes were not.
- A real packet landing in the window was not shown, only forced.

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
| 2026-10-02 18:43 | flags, `-D BENCH_ALARM_RACE_PROBE=1` | `a52e900-dd3fab8-o55518b` | measurement | `tools/arclog/runs/20261002T184356Z-a52e900-dd3fab8-o55518b/` | 8 us steps: program win 288 us, 35 landings, 0 dropped. |
| 2026-10-02 18:46 | flags, `=2` (bare relock) | `a52e900-db11b2e-odfd4c6` | measurement | `tools/arclog/runs/20261002T184626Z-a52e900-db11b2e-odfd4c6/` | Positive control, 8 us steps: 0 dropped, so the probe was not yet trusted. |
| 2026-10-02 18:48 | flags, `=2` | `a52e900-d5da271-odfd4c6` | measurement | `tools/arclog/runs/20261002T184829Z-a52e900-d5da271-odfd4c6/` | 1 us steps: 281 landings, 0 dropped. |
| 2026-10-02 18:51 | flags, `=2` | `a52e900-d525c20-odfd4c6` | measurement | `tools/arclog/runs/20261002T185107Z-a52e900-d525c20-odfd4c6/` | Landing classes added: program 255 / 10 / 17, 0 dropped with a bare relock in the middle; the cancel line is invalid (baseline taken in the wrong state). |
| 2026-10-02 18:53 | flags, `=2` | `a52e900-de56395-odfd4c6` | measurement | `tools/arclog/runs/20261002T185330Z-a52e900-de56395-odfd4c6/` | `PROBE_CTL`: after a relock, `CR.ALRAE` clears and `ALRMAR` changes. Cancel baseline fixed: 0 dropped. |
| 2026-10-02 18:55 | flags, `=2` | `a52e900-d33fe54-odfd4c6` | measurement | `tools/arclog/runs/20261002T185542Z-a52e900-d33fe54-odfd4c6/` | `PROBE_CTL2`: `ALRMAR` takes writes after a relock, an unlock and a wrong key. |
| 2026-10-02 18:58 | flags, `=2` | `a52e900-d715fbe-odfd4c6` | measurement | `tools/arclog/runs/20261002T185830Z-a52e900-d715fbe-odfd4c6/` | `PROBE_CTL3`: `ALRMBR` and `ICSR.INIT` refuse writes after a relock. |
| 2026-10-02 19:01 | flags, `=2` | `a52e900-d6e877b-odfd4c6` | measurement | `tools/arclog/runs/20261002T190103Z-a52e900-d6e877b-odfd4c6/` | `PROBE_CTL4`: protection map. |
| 2026-10-02 19:03 | flags, `=1` | `a52e900-d48a40e-o55518b` | measurement | `tools/arclog/runs/20261002T190319Z-a52e900-d48a40e-o55518b/` | The table above: real ISR writer, `WUTR` added to the map; program 255 / 10 / 17, cancel 23 / 0 / 21, 0 dropped. |

Every run passed the boot check and the `PROBE_ALARM` expectation.
