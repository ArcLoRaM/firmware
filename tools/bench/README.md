# bench

Host tool that builds, flashes and tests the ArcLoRaM firmware on the bench boards without a human in the loop.
It is what lets an agent (Claude Code) close the edit, build, flash, check loop on its own.

The design and the reasons behind it are in `docs/adr/`, the vocabulary in `CONTEXT.md`.
This folder holds everything about the bench automation: the records stay out of the firmware's own `docs/adr/` and `CONTEXT.md`.

## Status

`bench build`, `boards`, `capture`, `flash`, `reset` and `run` with scenarios work (#62, #63, #64).
Work is tracked as GitHub issues with the `bench` label.

## Setup

```sh
cd tools/bench
uv sync
uv run pytest
```

Run it from anywhere in the repo with `uv run --project tools/bench bench ...`.

## Build

```sh
bench build --class C2                      # both cores of Debug_C2
bench build --class C2 --class C3 -D TX_RAMP_MS=5u
bench build --class C3 --clean              # rebuild everything
```

1. Rsyncs the repo (uncommitted changes included) into the Build Tree, `C:\Users\Simon\arcfw-bench\tree`, without `.git`, `tools`, `Tests`, `docs`.
   Build outputs (`Debug_*`) and the override header in the tree are kept, so builds are incremental.
2. Computes the Build ID (`a1b2c3d`, `-d<hash>` for uncommitted firmware changes, `-o<hash>` for overrides) and writes it with the overrides to `Common/Bench/bench_overrides.h` in the tree, only when it changed.
3. Builds both cores of each configuration with `stm32cubeidec.exe` headless, in its own workspace (`arcfw-bench\workspace`): the developer's CubeIDE can stay open.
4. Maps every compiler diagnostic back to a repo path, and checks that each ELF contains the Build ID.
   An image without it after an error-free build holds stale objects (make misses a header that appeared after the objects were compiled); that configuration is rebuilt clean once.

Exit code 0 when every image is built and carries the Build ID, 1 otherwise; the full CubeIDE output is kept in `arcfw-bench\logs\build-<id>.log`.
A first build takes about a minute, a build with nothing to recompile about 20 s.

## Boards

```sh
bench boards                  # probe, COM port, UID, Node ID, last build
bench boards --probe-uids     # also read unknown UIDs over SWD (reboots those boards)
```

A board is found by its ST-LINK serial number; its trace COM port is the one whose USB parent is that probe (Windows device tree).
Its UID comes from the last CM0+ `BOOT` in the capture, and its Node ID from the firmware's table, `Common/Protocol/node_id.c`, parsed in its strict one-entry-per-line format.
Reading a UID over SWD needs a connection under reset: the firmware sleeps in STOP2, where the debug port is off.

## Capture

```sh
bench capture status
bench capture up              # start or complete the always-on capture
bench capture up --replace    # also stop another capture holding the ports
```

One multi-port `arclog capture` on Windows records every connected probe's port into `tools/arclog/runs/bench/` (files `com9-YYYYMMDD.log`, stderr in `capture.err`).
It is started detached (`Win32_Process.Create`) and outlives the session.
`up` restarts the bench capture when a port is missing from it, and never stops a capture it did not start unless told to (`--replace`).

## Flash

```sh
bench flash --node 2=C2                  # build Debug_C2, flash Node ID 2, check its boot
bench flash --node 1=C3 --node 2=C2 -D TX_RAMP_MS=5u
```

1. Finds the boards, brings the capture up, builds the classes needed.
2. For each board, reads its UID over SWD and refuses to flash if it is not the expected Node ID (a board moved since its last boot).
3. Writes and verifies both cores' images in one programmer session (CM0+ then CM4), then resets.
4. Waits (`--timeout`, 60 s) for both cores to `BOOT` the new Build ID as the assigned class, linked, with no lost line (the arclog `expect` engine); a reboot of the old image during the flash is waited through.

Exit code 0 when every board booted the build, 1 otherwise.

```sh
bench reset 2                            # reset Node ID 2, no flash
```

## Run

A run flashes a scenario's boards, fires its actions, decides it from the capture and keeps a record.

```sh
bench scenario check scenarios/c2-rejoin-after-reset.toml    # validate and show the plan, no board touched
bench run scenarios/c2-rejoin-after-reset.toml
bench run --node 1=C3 --node 2=C2 --expect "2 CLK to=WARM within=8m" --save scenarios/mine.toml
```

Exit code 0 pass, 1 fail, 2 timeout, 3 invalid scenario.
Progress is printed as it happens (`ok ...`, `act ...`, `note ...`), and the record goes to `tools/arclog/runs/<start>-<build>/`: `report.md`, `run.log`, `scenario.toml` and each node's capture lines of the run window (readable by `arclog report`).

### Scenarios

Boards are named by Node ID.
The smallest scenario is two lines:

```toml
[nodes]
2 = "C2"
```

A fuller one ([`scenarios/`](scenarios/) has examples to copy):

```toml
description = "C2 rejoins after a reset"
timeout = "20m"                  # default 10m

[nodes]
1 = "C3"                         # flash as C3
2 = "C2"                         # flash as C2
3 = "watch"                      # not flashed: its trace is checked, it keeps its image

[overrides]                      # Build Overrides for this run only
TX_RAMP_MS = "5u"

[[action]]
reset = 2                        # reset Node ID 2...
after = { node = 2, event = "CLK", where = { to = "WARM" } }
delay = "10s"                    # ...10 s after that line (or: at = "+90s" after arming)

[[expect]]
node = 2                         # optional: any node
event = "CLK"
where = { to = "WARM" }          # optional: field values; a number range, bounds inclusive:
                                 #   where = { err = { min = -1, max = 1 } }
count = 2                        # optional, default 1
within = "18m"                   # optional: from arming, default the timeout

[[forbid]]
event = "TX_LATE"
```

- The Smoke Check is always on: every flashed board boots the run's Build ID on both cores, as its class, linked, with no lost line.
- Every reset must reboot its node (the run cannot pass before it), and no other reboot is allowed.
- Event and field names are checked against arclog's event table, with a suggestion for a typo (`unknown event 'SYNC_RXX' (did you mean SYNC_RX?)`).
- A range never matches a field that is not a number; `{ min = 2 }` or `{ max = -2 }` alone bound one side.
- On the command line, `--expect "<ID|any> <EVENT> [field=value ...] [within=8m] [count=2]"`, a range as `err=-1..1`, `err=..-2` or `err=2..`, `--watch ID`, `--forbid EVENT`, `-D NAME=VALUE`, `--timeout`; `--save FILE` writes them as a scenario file.

Reset is the only action for now.
Halting a core needs a hot-plug SWD connection, which fails while the firmware sleeps in STOP2 with the debug port off; it needs the firmware to keep debug alive in STOP2 first.

### What bench never does to a board

Every programmer call goes through one allowlist (`src/bench/programmer.py`): connect to a named probe, read the chip UID, write an ELF image, verify, reset.
Everything else is refused before the programmer runs, in particular anything that cannot be undone by flashing again:

- option bytes: readout protection (level 2 is permanent), write protection, boot configuration, security;
- OTP, the one-time programmable area (`0x1FFF7000`-`0x1FFF73FF`);
- mass erase, direct memory writes, binary files written at a given address.

Before writing, every loadable segment of each image must fall inside its own core's half of main flash (CM4 `0x08000000`-`0x0801FFFF`, CM0+ `0x08020000`-`0x0803FFFF`); anything else, including a swapped image, is refused.

## Overview

`bench` is a Python CLI (uv project, like `tools/arclog`) run from WSL.
It drives only Windows tools through WSL interop, so the ST-LINK probes and their serial ports stay on Windows and nothing is attached with `usbipd`:

- `stm32cubeidec.exe` (STM32CubeIDE headless build),
- `STM32_Programmer_CLI.exe` (flash, UID read, reset, halt),
- `arclog` through `uv.exe` (capture and checks).

Commands:

| Command | Does |
|---|---|
| `bench build` | Syncs the working tree to the Build Tree and builds the configurations a run needs |
| `bench flash` | Flashes both cores of the named boards from one build, then resets them |
| `bench run <scenario>` | Build, flash, arm, fire the scheduled actions, check the expectations, report |
| `bench reset <node>` | Resets or halts/resumes a board without flashing |
| `bench capture up` / `status` | Starts or checks the always-on capture of every board |

A quick run without a scenario file uses the same machinery:

```sh
bench run --node 1=C3 --node 2=C2 -D TX_RAMP_MS=5
```

## Rules

- `bench` is the only way to build or flash the firmware from an agent: never the toolchain, `make` or `STM32_Programmer_CLI` directly.
- `bench` never writes option bytes or OTP, never changes readout protection or security, and never mass erases (see [What bench never does to a board](#what-bench-never-does-to-a-board)).
- `bench` never commits: a run ends with a report, and the human decides what goes in.
