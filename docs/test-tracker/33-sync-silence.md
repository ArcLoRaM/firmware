# Issue #33: wall-clock Sync silence timeout

Purpose: acceptance. Issue: #33 (closed 2026-10-01).

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/c2-sync-silence.toml` | A WARM C2 without the C3 logs `SYNC_SILENCE`, then `CLK to=COLD why=silence` |
| `tools/bench/scenarios/midnight-c3-c2.toml` | No false timeout while the C3's duty-cycle denials leave gaps of about 100 s (forbids `SYNC_SILENCE`) |

## Hardware

| Item | Needed |
|---|---|
| Node 1 flashed as C3, probe `003D003D3234510833353533` | yes |
| Node 2 flashed as C2, probe `003E002E3333511431363730` | yes |

Hands on the bench: unplug node 1's USB once the C2 is WARM; plug it back only after the run exits (a reboot the run did not fire fails it).

## Criteria

Host tests in `Tests/unit/test_mac_state_machine_c{1,2,3}.c` (`c*` = `c1` and `c2`):

- [x] `MAC_CheckSyncTimeout` in `mac_state_machine.h`.
- [x] Any received Sync packet (Tier 1, 2, 3) resets the timer: `test_c*_tier2_resets_silence_timer`, `test_c*_tier3_resets_silence_timer`.
- [x] WARM: 14 min no change, 15 min to COLD: `test_c*_warm_14min_silence_no_degradation`, `test_c*_warm_15min_silence_degrades_to_cold`; bench `c2-sync-silence.toml`.
- [x] ACQUIRING: 15 min to COLD: `test_c*_acquiring_15min_silence_degrades_to_cold` (host only: a C2 spends about 6 s in ACQUIRING).
- [x] COLD no-op: `test_c*_cold_timeout_is_noop`. C3 no-op: `test_c3_sync_timeout_is_noop`.
- [x] A packet at 14 min resets the timer: `test_c*_sync_at_14min_resets_timer`; bench `midnight-c3-c2.toml` (no false timeout).
- [x] Tier 3 immediate degradation unchanged: `test_c*_tier3_immediate_degradation_unchanged`.
- [x] Beyond the issue, silence across midnight: `test_c*_silence_timeout_spans_midnight`; bench run below.

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
| 2026-10-01 21:38 | `midnight-c3-c2.toml` | `2421333-o8f3c06` | PASS | `tools/arclog/runs/20261001T213849Z-2421333-o8f3c06/` | No `SYNC_SILENCE` in 20 min of sparse C3 packets. |
| 2026-10-01 22:00 | `c2-sync-silence.toml` | `2421333` | PASS | `tools/arclog/runs/20261001T220044Z-2421333/` | Node 1 unplugged at 22:05:47. `SYNC_SILENCE last=86370021 now=872828` (902 807 ms, across 00:00), then `CLK WARM -> COLD why=silence`, `MAC_ST SYNC -> SCAN`, `SCAN`. |

GitHub: results commented on #33, closed 2026-10-01.
