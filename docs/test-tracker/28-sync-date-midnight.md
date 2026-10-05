# Issue #28: Sync date across midnight on hardware

Purpose: acceptance. Issue: #28.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/midnight-date-c3-c2.toml` | A C2 reset 1 s into a phase that starts at 23:59:57 sets its RTC from cell 1 or 2 (after midnight) with the next day's date: `RTC_SET date=000102` (the C3 boots on 2000-01-01, `date=000101`). On the build before the fix the same set carries `date=000101` and the scenario fails |

Host tests cover the rest: `test_date_bcd` (month ends, leap years, year end, both directions), the C2 and C1 MAC tests (cell after midnight, carry across midnight, Tier 3, nominal day) and the C3 MAC tests (epoch day against the RTC read).

## Hardware

| Item | Needed |
|---|---|
| Node 1 flashed as C3, COM8, probe `003D003D3234510833353533` | yes |
| Node 2 flashed as C2, COM9, probe `003E002E3333511431363730` | yes |
| Host on AC, sleep off for the run (about 4 min after the build) | yes |

Hands on the bench: none.

## Criteria

- [x] Rollover detected where the target time is known, in the MAC `rtc_set` call path (host: `test_mac_state_machine_c2`, `_c1`).
- [x] A cell after midnight in a phase that started before it sets the next day's date (host, and bench: `midnight-date-c3-c2.toml`, PASS 2026-10-02).
- [x] The nominal path is unchanged: same day, same date (host: `test_c2_cold_set_in_the_epoch_day_keeps_the_date`, `test_c1_cold_set_in_the_epoch_day_keeps_the_date`).
- [x] The Sync phase date is the epoch's date when C3's RTC read is across midnight from it (host: `test_mac_state_machine_c3`).
- [x] CONTEXT.md and ADR-0005 describe the rule.
- [x] Red on the build before the fix: the same scenario on `cf35ee9` fails, the C2 sets `date=000101` from cell 1 after midnight (2026-10-02, `cf35ee9-dd04967-od6ea81`).

The scenario times the reset from arming (`at = "+118s"`), so a pass needs the C2 to be cold for cell 1 or 2 of the straddling phase: if the reset lands elsewhere, the expectation times out, which is a bench timing result, not a verdict on the firmware. The phase grid hangs on the C3's boot, so the epoch's last digits vary per boot (026-029 ms) and the scenario never matches them.

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
| 2026-10-01 23:15 | `midnight-date-c3-c2.toml` (first version) | `cf35ee9-da9e8c7-od6ea81` | FAIL (scenario) | `tools/arclog/runs/20261001T231553Z-cf35ee9-da9e8c7-od6ea81/` | Wrong expectation, no verdict on the firmware: the scenario matched `ep=86397026` but this boot's epoch was `86397029`, so the reset never fired. The trace shows C3's calendar rolling to `000102` at midnight and the C2 staying WARM across it. |
| 2026-10-01 23:24 | `midnight-date-c3-c2.toml` (reset after C3 `RTC_SET`) | `cf35ee9-da9e8c7-od6ea81` | FAIL (scenario) | `tools/arclog/runs/20261001T232409Z-cf35ee9-da9e8c7-od6ea81/` | Wrong expectation, no verdict on the firmware: C3's boot `RTC_SET` is logged before the run arms, so the `after` trigger never saw it and the reset never fired. Fixed with `at = "+118s"`. |
| 2026-10-02 08:37 | `midnight-date-c3-c2.toml` (reset `at = "+118s"`) | `cf35ee9-da9e8c7-od6ea81` | PASS | `tools/arclog/runs/20261002T083747Z-cf35ee9-da9e8c7-od6ea81/` | Reset at 00:00:00 on C3's clock. C2's Packet 1 was cell 1 of the phase that started at 23:59:57 (`SYNC_RX ph=0 ce=1 ep=86397029 act=set`), then `RTC_SET new=1030 date=000102`; cell 2 `good`, WARM again at `ce=0` of the next phase (+156 s). No `SLOT_SUSPECT`, no `SYNC_SILENCE`. |
| 2026-10-02 08:43 | `midnight-date-c3-c2.toml` (same) | `cf35ee9-dd04967-od6ea81` | FAIL (expected, red) | `tools/arclog/runs/20261002T084338Z-cf35ee9-dd04967-od6ea81/` | Build before the fix (the three MAC sources at `cf35ee9`). Same timing: `SYNC_RX ph=0 ce=1 ep=86397029 act=set`, then `RTC_SET new=1030 date=000101`: the C2's calendar reads 1 Jan at 00:00:28 and later while the C3's reads 2 Jan. `RTC_SET date=000102` never appears. |

Three earlier attempts on 2026-10-01 never reached the boards: the build failed with missing-HAL link errors (see below).

## Bench build alternation (issue #73)

Builds of the Build Tree on `C:` alternated: any build right after a successful one failed with 146 (CM4) and 393-417 (CM0+) link errors, undefined `HAL_*`; any build right after a failed one succeeded.
Ten builds in a row followed it (2026-10-01 and 2026-10-02), whether run by `bench build` or `bench run`, and across a Build ID change.
It was not caused by this issue's change (CM4 is untouched).
Cause and fix: the sync deleted folders CubeIDE makes for linked files, see `spot-20261005-build-alternation.md`.
The throwaway failing build before a run is no longer needed once that fix is on `main`.
