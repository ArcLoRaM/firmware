@AGENTS.md

## Agent skills

### Commit conventions

We use [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/)
(`feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`, etc.). Scope is
optional but encouraged when the change is focused (`fix(tdma):`,
`docs(context):`).

### Issue tracker

Issues live in GitHub Issues (`github.com/ArcLoRaM/firmware`). See `docs/agents/issue-tracker.md`.

### Triage labels

Default label vocabulary (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Hardware list for scenarios

Every scenario or bench run you propose to Simon comes with its hardware list:
each board by Node ID with the class it is flashed as, the probes and COM ports it uses, anything beyond plugged-in boards (cables, a USB hub, an antenna or attenuator, a power switch), and every step that needs hands on the bench (a replug, a CubeMX regeneration).
Check the list against `bench boards` and say which items are missing today.

### The repo holds every test fact

Every test protocol, result, finding and run record name lives in the repo first: `docs/test-tracker/` (Test Records) and `tools/bench/scenarios/`.
An artifact or shared doc is only a view of them: write the fact to the repo before (or with) the artifact, never to the artifact alone.

Test workflow: `docs/test-tracker/README.md` (test levels, issue-record-scenario link, verdicts, closing rule). Read it before writing a Test Record or a scenario, and before closing an issue on a bench verdict.

### Timing measurements

Timing on the boards (a budget, a latency, time lost in a write) is measured, not estimated: skill `timing-probe` (a temporary SysTick probe behind a Build Override, numbers kept in the Test Record, probe never committed).

Probe uses so far: 1 (#55, 2026-10-01).
Add one here at each probe run of a new topic. At 3, propose to Simon a committed probe helper instead of hand-written probes: a header with start/mark/log macros compiled only under a Build Override, and a permanent generic `PROBE` event in arclog, which removes the schema edit and the clean-up from every measurement.

### Domain docs

Single-context layout — one `CONTEXT.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.
