# Issue #82: finer SyncStamp, in RTC ticks

Purpose: acceptance. Issue: #82.

The clock is the NUCLEO Clock (CONTEXT.md): every figure of this record is a NUCLEO result and does not transfer to the Production Clock.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/fine-stamps-c3-c2.toml` | C3 (Node 1) + C2 (Node 4), nothing injected, 60 min on the 120 s schedule: `SYNC_RX` carries `erru`; its scatter at rest (sigma) is read from the run record and compared with the 0.36 ms of the whole-ms `err`. Run 2026-10-04: PASS (see Result) |

## Hardware

Checked against `bench boards` on 2026-10-04: 2 boards connected, with known Node IDs.

| Item | Needed | Today |
|---|---|---|
| Node 1 flashed as C3, probe `003D003D3234510833353533`, COM8 | yes | connected |
| Node 4 flashed as C2, probe `004D00303333511431363730`, COM10 | yes | connected |
| Host on AC power for 75 min, Windows sleep off | yes | per session |

Hands on the bench: none while the two boards stay plugged in.
No CubeMX regeneration.

## Host level (done, branch `worktree-82-fine-stamps`)

- `test_sync_stamp`: the stamp from RxDone, the air time and the latency in ticks, across midnight; the error in us to the tick against an exact schedule ms (worked by hand: one tick late is 244 us, 12 ticks 2930 us, 4 ticks before midnight against 100 ms after it is -100 977 us); the ms to tick conversion round-trips.
- `test_lora_toa`: the time on air from the formula, not measured: 991 232 us for the Sync packet (SF12/BW125, 10 bytes), 41 216 us at SF7, 2 465 792 us for 51 bytes at SF12, implicit header, no CRC and CR4/8; checked against an independent script.
- `test_mac_fine_stamp_c1` and `_c2`: `SYNC_RX` logs `erru`; Tier 1 ends between 32 and 33 ticks (7813 / 8057 us), Tier 3 begins between 409 and 410 ticks; the acquisition check has the same bound; the sample hook gets the error in us; across midnight; the ms entry point still works. The whole-ms stamp could not tell these apart.
- `test_drift_estimator`: the estimator in us keeps the sub-ms part (a perfect 8.1 ppm line is recovered to 20 ppb); at the validity gate, over 2000 seeds, the rate error falls from 182 ppb rms (worst 720) with the whole-ms stamp to 119 ppb rms (worst 465) with the tick stamp, as the noise goes from 0.36 to about 0.23 ms in the simulation.
- The existing MAC tests keep their meaning: their `SYNC_RX` assertions gained `erru`, their sample hook assertions are in us.
- The firmware builds (`32a8d25-dc97fc9`, same 6 warnings); the new code costs about 1 KB of flash.

## Result (2026-10-04, Node 1 as C3 and Node 4 as C2, NUCLEO Clock)

**The scatter of the Sync error at rest.** 28 Tier 1 packets over 60 minutes, 120 s apart, nothing injected, register at 0 (`fine-stamps-c3-c2.toml`, build `1bfc812-o599ce4`):

| | Stamp in ticks (`erru`) | Whole ms (`err`, rounded) |
|---|---|---|
| Scatter about the fitted line | **106 us** | 304 us |
| Successive differences / sqrt(2) | 111 us | 303 us |
| Fitted trend | +0.594 ppm | +0.499 ppm |

The tick stamp has a scatter 2.9 times lower on the same packets, and the trend is the pair's own +0.59 ppm of #34 (the whole-ms one is biased by its quantisation). The `erru` values step in ticks (244 us). In simulation this takes the first valid rate estimate from 182 to 119 ppb rms.

**There is no large constant offset (a correction).** The first two packets after the set read 227 and 471 us, then the error follows the drift (+71 us per 120 s packet, 2.7 ms in the hour). The earlier reading of a constant +1.4 ms at rest (#34, #45, #83) was the same drift on the whole-ms stamp: in the 40 min natural-pair run its `err` stepped from 1 to 2 ms after 24 to 28 min, where +0.59 ppm crosses a millisecond. Any real constant in the stamp or the Tx and Rx paths is at most about 0.25 ms.

**Packet 1 set (#55).** `c2-packet1-set-offset.toml` run on Node 4 (a copy naming Node 4, outside the repo): PASS twice (builds `98a6805`, `d8d47a2`). The two acquisition packets read 0 ms (227 us) after the set: the finer stamp does not move them out of the -1..1 ms range.

**CPU cost of the stamp** (`PROBE tag=rx_stamp`, CM0+ at 4 MHz). The first version (the LoRa formula and 64-bit conversions in the interrupt) cost 545 to 557 us per packet, against 133 us for the driver's own whole-ms air time: worse. Converting the air time and the Rx latency to ticks once per length, with a 32-bit subtraction in the interrupt, and taking the log's ms from the same cache, costs **65 us per packet** (477 us for the first packet, which fills the cache): cheaper than the 133 us it replaces. No new wake-up, timer or radio time.

## Criteria

- [x] Host: the error in ticks and in us across midnight, the ToA in us, the wrappers.
- [x] Host: the estimator fed in us; `SYNC_RX` carries `erru`; schema test passes.
- [x] Existing MAC and TDMA tests pass unchanged in meaning.
- [x] Bench, C3 + C2: sigma of `erru` at rest measured and compared with the 0.36 ms of `err` (NUCLEO Clock); the CPU cost of the stamp is not worse (`PROBE`). (106 us against 304 us on the same packets; 65 us per packet against 133 us.)

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
| 2026-10-04 19:43 | `fine-stamps-c3-c2.toml` | `1bfc812-o599ce4` | PASS | `tools/arclog/runs/20261004T194503Z-1bfc812-o599ce4/` | 60 min, 28 Tier 1 packets: scatter of `erru` 106 us against 304 us for `err` on the same packets; trend +0.594 ppm; first packets after the set 227 and 471 us. The first attempt (19:41) did not start: a capture restarted in the worktree has no `BOOT` in it, so the UIDs were unknown (`--probe-uids` fixes it) |
| 2026-10-04 21:09 | `c2-packet1-set-offset.toml` (a copy naming Node 4) | `98a6805` | PASS | `tools/arclog/runs/20261004T210929Z-98a6805/` | The Packet 1 set precision of #55 with the stamp in ticks: both acquisition packets within -1..1 ms |
| 2026-10-04 21:13 | flags: `--node 1=C3 --node 4=C2 -D BENCH_PROBE=1 --expect "4 PROBE tag=rx_stamp count=3"` | `98a6805-od66516` | measurement | `tools/arclog/runs/20261004T211337Z-98a6805-od66516/` | The first version of the stamp: 133 us (driver air time) then 545 to 557 us (stamp in ticks) per packet: worse than the old path |
| 2026-10-04 21:18 | same | `d8d47a2-od66516` | measurement | `tools/arclog/runs/20261004T211844Z-d8d47a2-od66516/` | The air time converted once: 69 us per packet after the first (455 us), the driver call still made for the log |
| 2026-10-04 21:22 | `c2-packet1-set-offset.toml` (Node 4 copy) | `d8d47a2` | PASS | `tools/arclog/runs/20261004T212254Z-d8d47a2/` | Packet 1 set precision again with the cached air time: `err=0`, `erru=227` on both acquisition packets |
| 2026-10-04 21:27 | same flags as 21:13 plus `--expect "4 RX_DONE toa=991"` | `de71790-od66516` | measurement | `tools/arclog/runs/20261004T212750Z-de71790-od66516/` | The final form: 65 us per packet (477 us for the first), the log's `toa=991` unchanged |

