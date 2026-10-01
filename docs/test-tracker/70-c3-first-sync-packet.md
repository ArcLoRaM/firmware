# Issue #70: the C3's first Sync packet after boot

Purpose: acceptance. Issue: #70 (closed 2026-10-01).

Fix: `TdmaMachine_Start` sets cell 0's channel before the chain's clock starts, so the radio's image calibration is paid at boot and not in the first slot's Tx lead.
Commit `6ed222e`.

## Host tests

| Test | File | Status |
|---|---|---|
| `test_c3_first_radio_channel_set_is_not_charged_to_first_slot` | `Tests/unit/test_tdma_machine_c3.c` | passing (red before the fix, `TX_LATE`) |
| `test_slot_task_calls_radio_set_channel` (adjusted: Start's channel set is not counted) | `Tests/unit/test_tdma_machine_c2.c` | passing |
| all 17 host suites (`ctest --preset host-linux`) | `Tests/` | passing |

The host test models the first channel set as a one-off cost.
It proves the order of events, not the real duration of the calibration.

## Bench

| Scenario | Proves | Status |
|---|---|---|
| `tools/bench/scenarios/c3-first-sync-packet.toml` | `SYNC_TX ph=0 ce=0` within 10 s of arming, no `TX_LATE` | PASS 2026-10-01, build `216774d` |

Run it with `bench run` once a board is plugged in.
Also check that cell 0 now leaves margin: compare the wake to `SLOT` stretch in the trace (8.5 ms before the fix, against a 16 ms budget).

### Hardware list

| Item | Needed | Present on 2026-10-02 |
|---|---|---|
| Board, Node ID 1, flashed as C3 | yes | no (`bench boards`: no ST-LINK probe connected) |
| Its ST-LINK probe and COM port | yes | no |
| Cables, hub, antenna, attenuator, power switch | no | not applicable |

Steps that need hands on the bench: plug in the C3 board with its probe.
No replug or CubeMX regeneration is needed.

## Exit criteria

- [x] Host tests green.
- [x] `c3-first-sync-packet.toml` green on a C3 board.
- [x] Issue #70 closed.

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
| 2026-09-29 00:33 | (trace of the always-on capture) | `66a35a2-d67ece1` | red | `tools/arclog/runs/20260929T003351Z-66a35a2-d67ece1/` | Before the fix: `wake=4`, `TX_LATE plan=86100026 fire=86100022 now=86100023`, no cell 0 packet. |
| 2026-10-01 21:38 | `midnight-c3-c2.toml` | `2421333-o8f3c06` | red (side check) | `tools/arclog/runs/20261001T213849Z-2421333-o8f3c06/` | Build without the fix: `TX_LATE plan=85800026 fire=85800022 now=85800023` at boot. |
| 2026-10-01 22:27 | `c3-first-sync-packet.toml` | `216774d` | PASS | `tools/arclog/runs/20261001T222725Z-216774d/` | Cell 0: `SLOT wake=5 nom=86100029` at .0192, `SYNC_TX send=86100025` (the fire instant, `nom - TX_RAMP_MS`), `TX_DONE start=86100029` = nominal. Expected wake to `SLOT`: 10.2 ms (was 12.5 ms). Cells 1-2 on time too. No `TX_LATE`. |
