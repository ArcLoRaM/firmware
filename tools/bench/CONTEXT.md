# Bench Domain Language

Vocabulary of the bench automation (`tools/bench`).
Firmware terms (Node Class, Node ID, ArcLog, ...) keep the meaning given in the repo root `CONTEXT.md`.

## Board

One NUCLEO-WL55JC on the bench, reached through its own ST-LINK probe.
The probe's serial number is how `bench` talks to a board, its chip UID is how `bench` recognizes it.
A board is addressed by its Node ID, found by reading the UID over SWD and looking it up in the firmware's UID table (`Common/Protocol/node_id.c`).
A board has no fixed Node Class: the Scenario picks one for each run.
_Avoid_: naming a board by its COM port, which changes with the USB port it is plugged into.

## Build Tree

The copy of the working tree (uncommitted changes included) that `bench` builds from, on the Windows disk (`C:\Users\Simon\arcfw-bench\`), with its own CubeIDE workspace.
It keeps agent builds away from the developer's IDE, workspace and `Debug_C*` folders, and the Windows compiler reads it at native speed.
It is synced from the repo before every build and is never edited by hand.

## Build Override

A define set for one run only (`-D TX_RAMP_MS=5`), written to a generated, gitignored `bench_overrides.h` in the Build Tree.
It sits next to `Common/Bench/bench_config.h`, which both cores' `.cproject` force-include (`-include`) in every C file and which includes it through `__has_include`, so a build without the file is the normal build.
It uses `#undef` then `#define`, so it can replace a `-D` of the build configuration; a define in a source file is overridable only when wrapped in `#ifndef`.

## Build ID

The identity of one build: git hash, dirty flag and a hash of the Build Overrides.
The firmware logs it in the `BOOT` line of both cores, which is how a run tells a fresh boot of the new image from a stale reboot of the old one.

## Scenario

A TOML file (`scenarios/*.toml`) that fully describes a run: the Node Class for each board, the Build Overrides, the Actions and the Expectations.
The command-line form (`--node 1=C3`) builds the same thing for quick runs.
The report copies the Scenario verbatim, so it is the record of what ran.

## Run

One execution of a Scenario: a time window over the Capture files, with defined edges.
- **Start**: the flash. The run is **armed** once every flashed board has printed a `BOOT` line with the expected Build ID.
- **Stop**: **pass** when every Expectation is met, **fail** on the first failure pattern, or **timeout**.

Boards the Scenario does not name are not flashed; the report gives the Build ID they last booted, without affecting pass or fail.
Output goes to `tools/arclog/runs/<date>-<build-id>/`.

## Action

Something a Scenario does to a board during a run, without flashing:
- **reset**: a reset over a hot-plug SWD connection (the image and Build ID stay the same),
- **halt / resume**: both cores stopped then restarted, a node going quiet (not a power loss: RTC and radio configuration survive).

An Action fires at an offset from the start of the run (`at = "+90s"`) or a delay after a trace event (`after = { node = 1, event = "SYNC_LOCK" }, delay = "10s"`).

## Expectation

A condition on the trace that decides a run, checked live by `arclog expect --follow`.
Every run includes the **Smoke Check**: within 30 s of the flash, each flashed board logs `BOOT` from both cores with the expected Build ID and assigned class, `CORE_SYNC stage=linked` and `id != 0`, with no second `BOOT`, no lost sequence numbers and no `!!` warnings.
A Scenario adds its own Expectations on top.

## Capture

The always-on, multi-port `arclog capture` process on Windows that records every board's trace UART (the ST-LINK virtual COM port) into files.
It is the only process that opens the COM ports; a run reads the files, it never opens a port.
`bench capture up` starts it if it is not running, and every run calls it first.
