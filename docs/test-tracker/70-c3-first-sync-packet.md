# Issue #70: the C3's first Sync packet after boot

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
| `tools/bench/scenarios/c3-first-sync-packet.toml` | `SYNC_TX ph=0 ce=0` within 10 s of arming, no `TX_LATE` | not run: no ST-LINK probe connected on 2026-10-02 |

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
- [ ] `c3-first-sync-packet.toml` green on a C3 board.
- [ ] Issue #70 closed.
