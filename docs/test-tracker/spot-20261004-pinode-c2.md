# Spot check 2026-10-04: a board on a Pi Node, flashed, observed and reset by bench

Purpose: spontaneous. Issue: #78 (item 3, boards on a remote setup).

First use of the Pi Node path of ADR-0002 on real hardware: bench builds, flashes both cores, reads the log and resets a board wired to a Raspberry Pi, with no ST-LINK in between.
The sender is the board on the other Pi Node, left running with its own firmware.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/pinode-c2-reset.toml` | Node 2 flashed as C2 through the Pi boots the build on both cores and is observed without a lost line (Smoke Check), one scripted reset reboots it exactly once, and it reaches `CLK to=WARM` from the sender's Sync |

## Hardware

Checked against `bench boards` on 2026-10-04: two local ST-LINK boards (Node IDs 1 and 4, in use by the #82 run) and both Pi Nodes answering on their log port; the board on `nuna-node-02` is Node 2.

| Item | Needed | Today |
|---|---|---|
| Node 2 flashed as C2: the board wired to Pi Node `nuna-node-02` | yes | present: UID `0026001a3232501420383543` read through the Pi's GDB on 2026-10-04 is Node ID 2 in `node_id.c`, so no onboarding; Node 2 is not on the local bench |
| A sender: the board on Pi Node `nuna-node-01`, running its own firmware | yes | log answers; left untouched, its Node ID is not needed |
| Pi Node `nuna-node-02`: OpenOCD (GDB 3333) and UART log (4000) | yes | log port answers; the GDB port has not been tried |
| Tailnet route from this PC (the bench tailnet), `~/.config/bench/bench.toml` | yes | present |
| CubeIDE `arm-none-eabi-gdb.exe` on the Windows host | yes | present (`C:\ST\STM32CubeIDE_2.1.1`) |
| Probes and COM ports | none, the Pi replaces the ST-LINK | the two local probes are not used |

Hands on the bench: none expected.
If GDB cannot attach while the firmware sleeps in STOP2, a person at the site restarts OpenOCD on the Pi or presses B4, and the run is repeated.

## Criteria

- [x] bench reads the board's UID through the Pi's GDB server without an ST-LINK (`bench boards --probe-uid nuna-node-02`, 2026-10-04 20:05 UTC: `0026001a3232501420383543`, Node ID 2; the node's log shows no BOOT, no lost line and no gap over 1.9 s around the read).
- [x] bench flashes both cores through the Pi and the images verify (`compare-sections`): the link fails the flash when fewer than two sections match, and the run passed.
- [x] The board boots the build as C2 on both cores, linked, with no lost line in the Pi Node's capture (Smoke Check, +10.8 s).
- [x] A scripted reset through the Pi reboots the board exactly once (reset at +90 s, one reboot at +102.4 s, `BOOT cls=C2 id=2` again).
- [x] The C2 reaches `CLK to=WARM` from the sender's Sync (+38.5 s after arming, sender untouched).

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
| 2026-10-04 20:05 | `bench boards --probe-uid nuna-node-02` (no scenario) | n/a | PASS (criterion 1) | `tools/arclog/runs/bench/nuna-node-02-20261004.log` | first GDB contact with a Pi Node: the firmware was in its normal Sync cycle (STOP2 wakes), the attach and the memory read worked; `reset halt` and `load` not yet tried |
| 2026-10-04 20:17 | `pinode-c2-reset.toml` | `32a8d25` | invalid (bench bug) | `tools/arclog/runs/20261004T201704Z-32a8d25/` | the flash through the Pi worked: both cores verified, the board booted as C2 `id=2` and locked onto the sender's Sync at +16 s. The Smoke Check ignored those boot lines because the flash time was taken after GDB exited, and `reset run` reboots the board inside the session. Fixed in `flash.py` (the time is taken before the programmer call), test added |
| 2026-10-04 20:21 | `pinode-c2-reset.toml` | `32a8d25` | invalid (build) | none | build failed with 146 CM4 and 426 CM0+ link errors: issue #73, a build right after a successful one fails; the failed build was the throwaway |
| 2026-10-04 20:23 | `pinode-c2-reset.toml` | `32a8d25` | PASS | `tools/arclog/runs/20261004T202320Z-32a8d25/` | first run through the Pi Node path end to end. Armed +10.8 s, WARM +38.5 s, reset +90 s, reboot +102.4 s, PASS +102.7 s. Trace: 108 lines, no `!!`, no sequence jump outside the two boots; the longest quiet stretch is 21.2 s, the node asleep, not capture loss. The sender on `nuna-node-01` last booted `build=dev`, its own firmware, and was not touched |
