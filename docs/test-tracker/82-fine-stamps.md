# Issue #82: finer SyncStamp, in RTC ticks

Purpose: acceptance. Issue: #82.

The clock is the NUCLEO Clock (CONTEXT.md): every figure of this record is a NUCLEO result and does not transfer to the Production Clock.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/fine-stamps-c3-c2.toml` | C3 (Node 1) + C2 (Node 4), nothing injected, 60 min on the 120 s schedule: `SYNC_RX` carries `erru`; its scatter at rest (sigma) is read from the run record and compared with the 0.36 ms of the whole-ms `err`. Written, not yet run (no board connected on 2026-10-04) |

## Hardware

Checked against `bench boards` on 2026-10-04: no probe connected.

| Item | Needed | Today |
|---|---|---|
| Node 1 flashed as C3, probe `003D003D3234510833353533`, COM8 | yes | not connected |
| Node 4 flashed as C2, probe `004D00303333511431363730`, COM10 | yes | not connected |
| Host on AC power for 75 min, Windows sleep off | yes | per session |

Hands on the bench: plugging the two boards in.
No CubeMX regeneration.

## Host level (done, branch `worktree-82-fine-stamps`)

- `test_sync_stamp`: the stamp from RxDone, the air time and the latency in ticks, across midnight; the error in us to the tick against an exact schedule ms (worked by hand: one tick late is 244 us, 12 ticks 2930 us, 4 ticks before midnight against 100 ms after it is -100 977 us); the ms to tick conversion round-trips.
- `test_lora_toa`: the time on air from the formula, not measured: 991 232 us for the Sync packet (SF12/BW125, 10 bytes), 41 216 us at SF7, 2 465 792 us for 51 bytes at SF12, implicit header, no CRC and CR4/8; checked against an independent script.
- `test_mac_fine_stamp_c1` and `_c2`: `SYNC_RX` logs `erru`; Tier 1 ends between 32 and 33 ticks (7813 / 8057 us), Tier 3 begins between 409 and 410 ticks; the acquisition check has the same bound; the sample hook gets the error in us; across midnight; the ms entry point still works. The whole-ms stamp could not tell these apart.
- `test_drift_estimator`: the estimator in us keeps the sub-ms part (a perfect 8.1 ppm line is recovered to 20 ppb); at the validity gate, over 2000 seeds, the rate error falls from 182 ppb rms (worst 720) with the whole-ms stamp to 119 ppb rms (worst 465) with the tick stamp, as the noise goes from 0.36 to about 0.23 ms in the simulation.
- The existing MAC tests keep their meaning: their `SYNC_RX` assertions gained `erru`, their sample hook assertions are in us.
- The firmware builds (`32a8d25-dc97fc9`, same 6 warnings); the new code costs about 1 KB of flash.

## To re-run on the boards before merging

- `tools/bench/scenarios/c2-packet1-set-offset.toml` (#55) asserts that the first error after a Packet 1 set is within +-1 ms (`err`). The whole-ms stamp was a floor, which reads on average 0.5 ms early; the stamp in ticks removes that, so the same packet now reads about 0.5 ms later and `err` (rounded) may leave the range. If so, the range is what changes, not the set: the clock is the same.
- `tools/bench/scenarios/c3-c2-sync.toml` and the other regression scenarios that lock a C2, for the tier decisions now taken in microseconds.
- The bias at rest changes with the stamp: the +1.4 ms of the natural run (#34) was measured with the floor; expect about +1.9 ms in `erru`. It is the constant of #83.

## Criteria

- [x] Host: the error in ticks and in us across midnight, the ToA in us, the wrappers.
- [x] Host: the estimator fed in us; `SYNC_RX` carries `erru`; schema test passes.
- [x] Existing MAC and TDMA tests pass unchanged in meaning.
- [ ] Bench, C3 + C2: sigma of `erru` at rest measured and compared with the 0.36 ms of `err` (NUCLEO Clock); the CPU cost of the stamp is not worse (`PROBE`).

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
