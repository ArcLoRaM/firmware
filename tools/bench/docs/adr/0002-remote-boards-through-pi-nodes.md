# Remote boards: bench reaches Pi Nodes over the tailnet

## Status

Proposed (2026-10-04, issue #78 item 3).
Built and host-tested, and exercised on a real Pi Node: UID read, flash of both cores, a scenario run and a reset passed on `nuna-node-02` (`docs/test-tracker/spot-20261004-pinode-c2.md`).
It stays proposed until the remaining open points at the end are settled and the collaborator has agreed the Pi-node changes (UTC stamp, recorder).

## Context

The Phase 1 baseline (#76) needs a 5-node line, which cannot be made on a desk.
It runs on a remote setup built by a collaborator: `github.com/Nuna-Systems/Pi-node`.
In that setup each NUCLEO-WL55JC is wired to its own Raspberry Pi (a Pi Node) and the onboard ST-LINK is bypassed:

- the Pi drives SWD from its GPIO header with OpenOCD: GDB server for the M4 on port 3333 and the M0+ on 3334, telnet and Tcl disabled;
- the Pi serves the MCU's UART (9600 8N1) as TCP on port 4000, one line per record, prefixed with the Pi's receive time of the line's first byte (Pi local time, no zone);
- the Pi joins the tailnet under its hostname (`nuna-node-01`, MagicDNS), and reaches the network by eduroam with a fallback Wi-Fi.

Today bench assumes a local ST-LINK on the Windows host: a probe serial number, `STM32_Programmer_CLI.exe`, a COM port from the Windows device tree, and a Windows `arclog capture` on that port.
A Pi Node has none of the three: no probe, no CubeProgrammer connection, no COM port.

What does not depend on where a board is: the build (Build Tree, headless CubeIDE), the scenario, the run engine and the record.
They read capture files and talk to a Board, nothing else.

## Decisions

**The seam is the programmer-shaped interface bench already calls.**
Discovery, flashing and runs ask four things of a board id: `probes()`, `read_uid(id)`, `flash(id, images)` and `reset(id)`.
The id is a probe serial number for a local ST-LINK board (`Programmer`) and the configured name for a Pi Node (`PiNodeLink`); a `Fleet` routes each call to the right adapter and merges the probe lists.
Two adapters and a fake in tests make the seam real, and no caller changed.
A Scenario still names Node IDs only: where a board is belongs to discovery, not to the scenario, so a run can mix local and remote boards.
A Node ID reachable twice (a local probe and a Pi Node) is an error, not a choice.

**A Pi Node is driven with GDB to its OpenOCD, in batch mode, behind an allowlist.**
It needs nothing on the Pi beyond what Pi-node installs, reads the ELF from the Build Tree on the controller (nothing is copied to the Pi), and uses the same command sequences as Pi-node's own `tools/flash.sh` and `tools/reset.sh`.
The client is CubeIDE's `arm-none-eabi-gdb.exe`, already on the Windows host, in line with ADR-0001 (Windows tools, nothing installed).
The guard keeps the shape of `programmer.py`: bench builds the whole command list from a small grammar and refuses anything else before GDB runs.
The grammar allows `target extended-remote <configured host>:<port>`, `monitor reset halt`, `monitor reset run`, `file` and `load` of an ELF that passed `check_image`, `compare-sections`, a read of the three UID words at `0x1FFF7590`, and `detach`.
It refuses every other `monitor` command (in particular `stm32l4x mass_erase`, `option_write`, `lock`, `unlock`), memory writes, `restore`, `shell`, `python`, `source` and script files.
Pi-node's `tools/erase.sh` is never used.
`compare-sections` exits 0 on a mismatch, so its output is parsed and a `MIS-MATCHED` fails the flash.
As today, the UID is read before any write and a board that is not the expected Node ID is not flashed: a board swapped at the remote site must be caught.

**The Pi Node's log is one more source of the same capture.**
`arclog capture` gets a TCP source next to the serial one (`--port tcp://nuna-node-01:4000/nuna-node-01 --node nuna-node-01`), through the line-source seam it already has; the path of the URL is the node's name, which bench uses to name the files.
It writes the same daily files, with the same host-time format, into the same directory, so `DirFollower`, `boot_check`, `follow` and `slice_capture` do not change.
A remote board's capture node is its configured name, not a COM port, so it is stable across replugs.

**Line time is the controller's UTC clock for the live stream, the Pi's UTC for the recorder.**
The Pi's stamp is its local time with no zone (measured: UTC+2), and the Pi-node README sets `Europe/Copenhagen`.
The EU clock change on 2026-10-25 repeats one hour, inside any 24 h session spanning that night, so a zone-less stamp cannot be trusted.
The live TCP source therefore drops the Pi's stamp and stamps each line on receipt, like a serial line; on a direct tailnet path that is about 10 ms later than the Pi's stamp, far below what ordering and run windows need.
The recorder below replays lines long after they were sent, so it needs the Pi's own time: it requires the Pi to stamp UTC (a small change proposed to Pi-node, or `timedatectl set-timezone UTC`), and bench refuses a Pi Node whose offset it cannot read as zero.
The clock skew between the Pi and the controller is measured at the start of a run and goes into the run manifest (#76 asks for the host time-sync state).

**Long runs record on the Pi and replicate by offset.**
The Pi's TCP stream keeps nothing: any drop of the link, or of the controller (a sleep ended the first 24 h capture, 2026-10-01), loses lines, and #76 makes a gap over 10 s an invalid run.
A small recorder on the Pi (a local client of port 4000 appending to a daily file) and a controller that pulls the file from its last offset make the capture exactly-once across link drops.
The live TCP source stays for short runs and for watching; the recorder is required before the 24 h run.
This is a change to Pi-node (an optional unit), to be agreed with its owner.

**A new board's first run is onboarding, the same for both kinds of board.**
A board whose UID is not in `Common/Protocol/node_id.c` boots with `id=0`, and its `BOOT` line carries the UID.
Onboarding reads that UID (from the `BOOT` line, or with `bench boards --probe-uids`) and adds the table entry itself, with the next free Node ID (highest assigned plus one) and a comment naming the board and its place.
The agent does not ask for the ID; it reports the one it chose.
The entry is a firmware change, left uncommitted like any other.
The board is then flashed once with a build that contains it, and the Smoke Check shows the new `id=`.
For a Pi Node the UID read is a GDB session (a debug connection to a shared board), or a reset of the board (button B4 at the site), which is a hands-on step.

**Configuration lists where to look, not what is there.**
A machine-level file (`$BENCH_CONFIG`, else `~/.config/bench/bench.toml`) lists the Pi Nodes:

```toml
[[remote]]
name = "nuna-node-01"      # capture node and display name
host = "nuna-node-01"      # MagicDNS name or tailnet IP
gdb  = 3333                # M4; the M0+ is gdb + 1
log  = 4000
```

Node IDs and classes stay out of it: a board is still recognised by its UID against `Common/Protocol/node_id.c` (ADR-0017 of the firmware), and its class is still the scenario's.
The file may also hold the Windows tool paths now hard-coded for one machine (`DEFAULT_ROOT`, `DEFAULT_CUBEIDE`, `DEFAULT_PROGRAMMER`); the `BENCH_*` environment variables keep overriding it.
It is per machine and not in the repo, so that every agent worktree sees the same remotes.

**A lost link is not a verdict on the firmware.**
When a remote capture lags or a Pi Node stops answering, the run is invalid (a bench fault, noted with its cause, run again), never FAIL.
Deadlines are held while a node's capture lags, so a link outage cannot fake a TIMEOUT.

**The network surface is the tailnet, with an ACL.**
Pi-node's GDB and log ports listen on all interfaces with no password, and a shared institutional tailnet holds many other people's devices.
Bench requires the Pi Nodes bound to their tailnet address and an ACL that lets only the bench controller (and the collaborator) reach ports 22, 3333, 3334 and 4000.
These are prerequisites to check and document, not something bench enforces.

## Considered Options

**A helper on the Pi, reached by an SSH forced command, enforcing the guard there.**
Defence in depth, a key that can do nothing else, and no GDB on the controller.
Rejected for now: it is code to maintain and version in the collaborator's repo, and the GDB path needs none.
Revisit if the open GDB port is judged too exposed.

**SSH to the Pi, copy the ELF, run OpenOCD there.**
Rejected: Pi-node's OpenOCD service holds the adapter, and its telnet and Tcl ports are disabled on purpose.

**OpenOCD's Tcl port from the controller.**
Rejected: disabled by Pi-node for lack of authentication, and `bindto` is global in OpenOCD, so it cannot be loopback-only next to a tailnet GDB port.

**One remote switch for the whole bench.**
Rejected: a per-board Link handles a mixed run (a local C3 with remote C2) for no extra code.

**A Windows host with ST-LINKs at the remote site, running the existing bench.**
Rejected: not the setup being built, and it keeps the USB passthrough problem ADR-0001 avoided.

## Consequences

- `Board` gains a `remote` flag; callers keep calling the programmer-shaped interface, now a `Fleet`.
- `arclog capture` gains a TCP source; `Capture.status` and `up` compare sources, not only COM ports.
- `flash` and `run` record the boards they work with and leave another worktree's capture alone when it holds only other ports, so a Pi Node can be tested while local boards are busy.
- `bench boards` lists Pi Nodes with their reachability (ports answering, last BOOT) and says so when one is unreachable.
- With the tailnet joined, the loop (UID read, flash, reset, log) needs only TCP 3333 and 4000 on the Pi.
  SSH is needed only if the spike shows that OpenOCD must be restarted to attach, and for the Pi-side recorder.
- A Pi Node's board needs an onboarding run before its first scenario (see above); until then `bench boards` lists it without a Node ID.
- Halt/resume (#71) and a real power cycle (#68) stay out of scope; a Pi that powers its board makes the latter possible later.
- `CONTEXT.md` gains Pi Node and Onboarding, and a reworded Board.

## Open until measured (spike on one Pi Node)

1. **Attach in STOP2.** Settled 2026-10-04: `reset halt`, `load` of both images, `compare-sections` and `reset run` worked against the Phase 1 firmware on `nuna-node-02`, which was in its normal Sync cycle (STOP2 wakes).
   No OpenOCD restart over SSH was needed; why the attach works while the firmware sleeps is not established.
2. **Flash time.** Measured 2026-10-04: UID read, both images written and verified, and reset take about 10 s over the tailnet from this PC (run start 20:23:20 UTC, first `BOOT` 20:23:30.7).
3. **UID read** through `monitor mdw`: settled 2026-10-04 (`0026001a3232501420383543`, Node ID 2 on `nuna-node-02`).
   Still open: the option bytes read before and after a flash, to show the path writes nothing else.
4. **Name resolution and routing from WSL.** Settled 2026-10-04: MagicDNS resolves from WSL and from Windows, and WSL opens TCP connections to `nuna-node-01` and `nuna-node-02` (ports 22 and 4000), so the controller needs no extra install for the log.
5. **Pi clock.** Measured 2026-10-04: the Pi stamps local time with no zone (20:54 against 18:54 UTC, so UTC+2 until the clock change on 2026-10-25).
   NTP state and the skew against the controller are still to measure.
