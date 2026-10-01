# Test tracker

One Test Record per bench test, committed here, so every scenario that was run keeps its purpose, its hardware and its verdicts in git.
Run records under `tools/arclog/runs/` stay on this machine; a Test Record names them, it does not replace them.

## Purpose

Every Test Record has exactly one purpose, which sets its file name and where its verdicts are reported:

| Purpose | File | Reported on GitHub |
|---|---|---|
| **acceptance**: proves an issue's acceptance criteria | `<issue>-<slug>.md` (`54-midnight-c3-c2.md`) | that issue: a comment per verdict; close it when every criterion is met |
| **performance**: measures a figure (drift, timing, current) | `perf-<slug>.md` (`perf-tx-start-delay.md`) | the issue the figure feeds, if any, as a comment |
| **spontaneous**: a one-off check outside any plan | `spot-<YYYYMMDD>-<slug>.md` | the issue it turns up or touches, if any, as a comment |

A spontaneous test that finds a defect becomes an issue; its Test Record then links it.

## Test Record

```markdown
# <issue or topic>: <what is tested>

Purpose: acceptance | performance | spontaneous. Issue: #<n> (or none).

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/<file>.toml` | <the behaviour, in trace terms> |

## Hardware

| Item | Needed |
|---|---|
| Node <id> flashed as <class>, probe <serial> | yes |

Hands on the bench: <unplug, replug, CubeMX regeneration, or none>.

## Criteria

- [ ] <one per acceptance item or measured figure, each with the host test or scenario that covers it>

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
| 2026-10-01 21:38 | `midnight-c3-c2.toml` | `2421333-o8f3c06` | PASS | `tools/arclog/runs/20261001T213849Z-2421333-o8f3c06/` | <what the record showed beyond the verdict> |
```

Runs are newest last and never deleted: a failed or invalid run stays, with its cause in Notes (a host sleep, a dead radio, a wrong expectation).
