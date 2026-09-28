# Agent-driven bench: build, flash and test through Windows tools from WSL

An agent builds, flashes and tests the firmware on the bench boards on its own, through one host tool, `bench`, and only through it.
`bench` runs in WSL and drives the Windows side of STM32CubeIDE through WSL interop: `stm32cubeidec.exe` in headless mode builds, `STM32_Programmer_CLI.exe` flashes, resets and reads chip UIDs, and `arclog` records the traces.
A run is described by a Scenario (Node Class per board, Build Overrides, scheduled Actions, Expectations) and decided by `arclog expect --follow` over the always-on Capture: pass, fail or timeout.
The agent loops (edit, build, flash, check) up to 5 times per task, never commits, and ends with the diff and the run report.

Until now every firmware build and flash was done by hand in CubeIDE, so the agent could propose a change but never see it run.

## Decisions

**Windows tools, no `usbipd`.**
The three ST-LINK probes and their virtual COM ports stay attached to Windows.
Everything needed (CubeIDE headless build, CubeProgrammer CLI, ST-LINK GDB server, GNU tools for STM32) already ships with STM32CubeIDE 2.1.1, so nothing is installed and no USB passthrough has to be re-attached after a replug or reset.

**CubeIDE managed build, in a separate Build Tree on the Windows disk.**
`.cproject` stays the source of truth, so the agent builds exactly what the developer builds.
The build runs on a synced copy of the working tree in `C:\Users\Simon\arcfw-bench\` with its own CubeIDE workspace: the developer keeps CubeIDE open (its workspace lock and `Debug_C*` folders are untouched), and the Windows compiler does not read every file over the `\\wsl` share, which is several times slower and delivers change notifications late.

**Build Overrides through a generated header.**
One-off defines (a parameter sweep) go into a gitignored `bench_overrides.h` in the Build Tree, included by the firmware with `__has_include`.
CDT's headless `-D` was not used: it can write the define back into the `.cproject` of the workspace project, which is the repo's.

**Build ID in the `BOOT` line.**
`bench` defines `BENCH_BUILD_ID` (git hash, dirty flag, override hash) in the same header, and the CM0+ `BOOT` line logs it next to `cls=`, `id=` and `uid=`.
It is the start edge of a run: without it, a fresh boot of the new image cannot be told from a reboot of the old one.

**Boards found by UID, classes chosen per run.**
`bench` reads each probe's chip UID over SWD (read-only) and looks it up in the firmware's UID table (`Common/Protocol/node_id.c`), which it parses from a strict one-entry-per-line format checked by a test.
There is no host-side board list to keep in sync with it.
A board has no fixed class: the Scenario says which Node ID is flashed as C2 or C3, and the Smoke Check verifies `cls=` in the `BOOT` line.

**Both cores flashed together, only the boards named.**
The CM4 and CM0+ images of one build are always flashed together (CM4 `0x08000000`, CM0+ `0x08020000`, 128K each), as the CM4 launch configuration already does.
The cores share MbMux memory and structure layouts, so mixed builds are a silent-corruption trap.
Only the boards a Scenario names are flashed; the others are reported with the Build ID they last booted.

**Always-on Capture, no board leases.**
One multi-port `arclog capture` process on Windows records every board and is the only process that opens the COM ports.
A flash shows up in it as a reset, which `arclog` already survives.
Runs are time windows over its files, followed by polling, not change notifications.
Boards are not leased: the agent owns the bench.

**Actions without flashing.**
A Scenario can reset a board (hot-plug SWD connection, `-rst`) or halt and resume both cores, at an offset from the start of the run or a delay after a trace event.

**Hard limits.**
`bench` never writes option bytes, never changes readout protection and never mass erases.

## Considered Options

**Native Linux tools in WSL with `usbipd`.**
Rejected: the ARM toolchain, CubeProgrammer and GDB would have to be installed in WSL, and every probe re-attached after each replug or board reset that re-enumerates it.

**Running the generated `Debug_C*/makefile` with CubeIDE's `make.exe`.**
Rejected: the makefiles only regenerate when the IDE builds, so a new source file is silently left out of the agent's build.

**CMake as the build source of truth.**
Deferred to a story: the cleanest end state (overrides and out-of-tree builds become native), but a migration of both cores and every configuration.

**Board leases or fixed soak/dev pools.**
Rejected: they add human bookkeeping, against the goal of a loop that needs as little human intervention as possible.

**A class column in the UID table.**
Rejected: a board is not tied to a class; the class belongs to the run.

## Consequences

- The firmware carries two small hooks for the bench: the `__has_include("bench_overrides.h")` include and the Build ID in the CM0+ `BOOT` line. The developer builds them once by hand; from then on the agent builds itself.
- `arclog` gains a multi-port `capture` and an `expect --follow` command that exits 0 (pass), 1 (fail) or 2 (timeout).
- A HardFault leaves nothing in the trace: the run fails on timeout with no cause. Batch GDB post-mortem is a story.
- Halt/resume is not a power loss. A real power cycle needs hardware (switchable USB hub or relay) and is a story.
- The repo's agent rules change from "never compile the firmware" to "build and flash only through `bench`".
