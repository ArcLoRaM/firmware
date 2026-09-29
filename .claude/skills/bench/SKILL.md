---
name: bench
description: Build, flash and test the firmware on the bench boards through `tools/bench`. Use when a firmware change needs a build, when it must be verified on hardware, when the user asks to flash, reset or run a scenario, or when board traces must be read.
---

# Bench loop

`bench` is the only way to build, flash or reset the firmware (`AGENTS.md`).
It works on its own Build Tree on `C:` and the boards on this machine; the developer's CubeIDE can stay open.
Command details, the scenario format and the guard live in [`tools/bench/README.md`](../../../tools/bench/README.md); `uv run --project tools/bench bench <cmd> --help` is authoritative for flags.
Vocabulary (Build ID, Scenario, Run, Action, Smoke Check) is in `tools/bench/CONTEXT.md`.

Run commands from the repo root as `uv run --project tools/bench bench ...`.
A run takes minutes: start it with `run_in_background`, and read its output file when notified (progress lines are flushed as they happen).

## The loop

A task that changes firmware behaviour runs this loop, at most 5 build-flash-check cycles; then stop and report what is known.

1. **Scenario.** Write or pick a scenario that goes _red_ on the problem and _green_ on the fix: boards by Node ID, the class each is flashed as, and an `[[expect]]` or `[[forbid]]` that names the behaviour itself, not just a boot.
   Start from `tools/bench/scenarios/`; a one-off can be flags (`--node 2=C2 --expect "2 CLK to=WARM within=8m"`), `--save` it when it is worth keeping.
   Done when `bench scenario check <file>` prints the plan you intend.
2. **Board state.** `bench boards` shows each probe, port, Node ID and last Build ID; `bench capture status` must show the bench capture recording every port (`bench capture up` otherwise).
   A board with an unknown UID needs `--probe-uids` (it reboots that board).
3. **Run.** `bench run <scenario>`: it builds the working tree (uncommitted changes included), flashes, fires the actions and decides.
   Done when it exits: 0 PASS, 1 FAIL, 2 TIMEOUT, 3 invalid scenario.
4. **Read the verdict**, then fix and go back to 3:
   - build failure: the diagnostics are printed with repo paths; the full log is in `C:\Users\Simon\arcfw-bench\logs\`.
   - `FAIL`: the reason names the node, the line and the time (`+12.3s`); read the node's lines around it in the record, `tools/arclog/runs/<start>-<build>/<node>-*.log`.
   - `TIMEOUT`: the reason lists what never happened; check the record for why (a board not booting, a peer silent, a sync never reached).
   - lost lines (`N line(s) lost`) are a firmware trace problem, not noise: find which lines vanished before changing the scenario.
   A verdict is evidence about the firmware; change the scenario only when it expected the wrong thing, and say so.
5. **Report.** The loop ends on PASS or at the cycle limit, with: the verdict, the Build ID, the record's `report.md` path, the diff, and anything the traces showed beyond the task.
   Commit only when the user asks.

## Guard

Every programmer call goes through the allowlist in `tools/bench/src/bench/programmer.py`: named probe, UID read, ELF write with verify, reset.
When bench refuses an operation, that refusal is final: report it to the user.
Option bytes, OTP, readout protection, security and mass erase are out of reach by design, and stay that way.

## Traces without a run

The bench capture writes every board's trace to `tools/arclog/runs/bench/<port>-YYYYMMDD.log` (UTC days).
Read those files directly, by polling (`tail`, `grep`, `arclog view`), or check a live condition with `uv run --project tools/arclog arclog expect <file> --dir tools/arclog/runs/bench --since <UTC time> --follow`.
