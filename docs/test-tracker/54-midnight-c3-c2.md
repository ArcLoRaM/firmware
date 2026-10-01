# Issue #54: midnight-safe slot timing on hardware

Purpose: acceptance. Issue: #54 (closed 2026-09-27 before its bench boxes; verified 2026-10-01).

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/midnight-c3-c2.toml` | C3 (started 23:50:00) and a locked C2 on the grid at 00:00:00.026 and 00:10:00.026; no `SLOT_SUSPECT`, `TX_TIMEOUT`, `SYNC_SILENCE` or C2 drop to COLD |
| `tools/bench/scenarios/c2-sync-silence.toml` | Silence timeout across midnight (shared with #33, see `33-sync-silence.md`) |

## Hardware

| Item | Needed |
|---|---|
| Node 1 flashed as C3, probe `003D003D3234510833353533` | yes |
| Node 2 flashed as C2, probe `003E002E3333511431363730` | yes |
| Host on AC, sleep off for the run | yes |

Hands on the bench: none for `midnight-c3-c2.toml`.

## Criteria

- [x] C3 + C2 across midnight: no `SLOT_SUSPECT`, no drop to `CLOCK_COLD`, `SYNC_RX` continuous through 00:00.
- [x] No `RX_CAP` / `TX_TIMEOUT` at 00:00: the Rx window spanning 00:00 timed out at 00:00:01.996, like every other cell (`x1.995`).
- [x] No `TX_DENIED` pattern change around 00:00: about one C3 `SYNC_TX` every 1.5-2 min before and after, no burst.
- [x] Tx on time after midnight: `TX_DONE start` equal to the slot's nominal start.
- [x] Silence timeout spanning midnight fires after 15 min and not before: 902 807 ms (`c2-sync-silence.toml`).

## Earlier evidence

The always-on capture of 2026-09-29 (`tools/arclog/runs/bench/com8-20260929.log`, `com9-20260929.log`, local) already crossed RTC midnight on builds with every #54 fix: no `SLOT_SUSPECT`, no drop to COLD (14 `CLK` lines over 7 C2 boots), C2 `SYNC_RX` `ep=86370026` at 23:59:31 then `ep=26` at 00:00:01 (`err=-5`, t1), C3 `TX_DONE start=26`, no `TX_TIMEOUT`, no `RX_CAP` in the slots around 00:00.
Not a run of record, hence the scenario runs below.
Two effects there only looked like midnight: C3 `TX_DENIED` from 00:01:30, which is the duty-cycle credit running out about 6.5 min after boot (a refill at midnight would have pushed it to about 00:06:30); and more C2 `RX_CAP` once the C3 is denied, false preamble detections in empty cells (#39, #58 topic).

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
| 2026-10-01 21:18 | `midnight-c3-c2.toml` | `2421333-o8f3c06` | FAIL (bench) | `tools/arclog/runs/20261001T211811Z-2421333-o8f3c06/` | Windows slept 21:23:39-21:30:55 on battery; both ports lost, midnight fell in the gap. No verdict on the firmware. |
| 2026-10-01 21:38 | `midnight-c3-c2.toml` | `2421333-o8f3c06` | PASS | `tools/arclog/runs/20261001T213849Z-2421333-o8f3c06/` | Both nodes at `nom=26` (+605 s) and `nom=600026` (+1205 s). C2 `err=-8` (t2) at 23:59:01, `err=0` at 00:00:31. One C3 `TX_LATE` at boot (#70, build before its fix). |
| 2026-10-01 22:00 | `c2-sync-silence.toml` | `2421333` | PASS | `tools/arclog/runs/20261001T220044Z-2421333/` | `SYNC_SILENCE last=86370021 now=872828`: 902 807 ms across 00:00. |

GitHub: results commented on #54 on 2026-10-01.
