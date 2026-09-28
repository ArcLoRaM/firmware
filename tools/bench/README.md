# bench

Host tool that builds, flashes and tests the ArcLoRaM firmware on the bench boards without a human in the loop.
It is what lets an agent (Claude Code) close the edit, build, flash, check loop on its own.

The design and the reasons behind it are in `docs/adr/`, the vocabulary in `CONTEXT.md`.
This folder holds everything about the bench automation: the records stay out of the firmware's own `docs/adr/` and `CONTEXT.md`.

## Status

Designed, not implemented yet.
Work is tracked as GitHub issues with the `bench` label.

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
