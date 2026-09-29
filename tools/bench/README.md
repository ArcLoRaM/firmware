# bench

Host tool that builds, flashes and tests the ArcLoRaM firmware on the bench boards without a human in the loop.
It is what lets an agent (Claude Code) close the edit, build, flash, check loop on its own.

The design and the reasons behind it are in `docs/adr/`, the vocabulary in `CONTEXT.md`.
This folder holds everything about the bench automation: the records stay out of the firmware's own `docs/adr/` and `CONTEXT.md`.

## Status

`bench build` works (issue #62); flash, run and capture are next (#63, #64).
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

## Overview

`bench` is a Python CLI (uv project, like `tools/arclog`) run from WSL.
It drives only Windows tools through WSL interop, so the ST-LINK probes and their serial ports stay on Windows and nothing is attached with `usbipd`:

- `stm32cubeidec.exe` (STM32CubeIDE headless build),
- `STM32_Programmer_CLI.exe` (flash, UID read, reset, halt),
- `arclog` through `uv.exe` (capture and checks).

Planned commands:

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
- `bench` never writes option bytes, never changes readout protection and never mass erases.
- `bench` never commits: a run ends with a report, and the human decides what goes in.
