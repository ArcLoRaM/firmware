# Node silences

A flashed node that stops logging while its capture is up.
One row per event, so that a cause is looked for when there is a pattern and not before.

Rule (Simon, 2026-10-04): the counter only counts until it passes 3.
The fourth event gets a proper investigation: an issue, the rows below as its evidence, and a scenario that waits for it.

**Count: 1**

## What counts

- The node logged normally, then no line for over 60 s while its capture connection was up: a COM port that is open, or a Pi Node whose log port accepts a connection.
- Not counted: a capture outage (a port down, the host asleep), a planned reset or reflash, a node that was never started.
- Found today by the age of the last line in `tools/arclog/runs/bench/<node>-YYYYMMDD.log` against the clock.
  #86 makes the capture say whether the connection was up, and a scenario can then ask for `max_silence`.
- On finding one: add a row here, then reset the node (`bench reset <Node ID>`).

## Events

| # | Last line (UTC) | Found | Node | Build | Silent | Recovery |
|---|---|---|---|---|---|---|
| 1 | 2026-10-04 20:37:31 | 21:11 | `nuna-node-02`, a C2 on a Pi Node | `32a8d25` (main) | 43 min | `bench reset 2` at about 21:21; `BOOT cls=C2 id=2` at 21:21:18, logging again |

## Event 1: what the trace shows

- The node ran normally for 12.5 min after its reset at 20:25:02: 243 slots, 26 relay transmissions, 12 Sync receptions.
- The last lines are `SLOT ph=0 ce=3 dec=TX`, `SYNC_TX`, `TX_DONE` (20:37:31). Then no `SLOT` (the next one, ce=4, is due 3 s later) and no `STOP2_WAKES`, on either core.
- The node had passed the same cell five times before (`ph=0 ce=3 dec=TX` at 20:26:30, 20:27:30, 20:29:30, 20:32:30 and 20:34:30, each followed by ce=4), so it is not that cell.
- The reset worked and the node booted and logged at once: the Pi, its UART link and its log server were fine, and the node was not logging.
- `nuna-node-01` (the sender, the collaborator's `dev` build) kept logging throughout.
- No bench command touched the node's debug port between its reset at 20:25 and the silence.

## Hypotheses for event 1 (none established)

1. **A lost Alarm A wake after the transmission: the symptom of #80.** It fits: the last line is the end of a Tx, nothing follows, the radio stays asleep, and the node stays silent until a reset. #80 says no cause has been observed, so this may be its first occurrence.
2. **Alarm A programmed from two contexts (#75).** `WAKE_ADJ` reprograms the alarm from the radio ISR and the slot task programs it from thread context, with no critical section. Against: the case #75 describes needs an RTC write, and none is logged in the last 12 s (no `RTC_SET`, no `RTC_SHIFT`, `DRIFT ... ok=0 trim=0`), and the last `WAKE_ADJ` came 5.7 s before the stop. For: nine `WAKE_ADJ` in the 12 minutes, so the interleaving was exercised.
3. **A hard fault or a core lockup.** A HardFault leaves nothing in the trace (ADR-0001). It cannot be excluded: reading the core needs a debug session, which bench does not allow and nobody asked for.
4. **Specific to this build.** The node ran main at `32a8d25`, `nuna-node-01` runs the collaborator's `dev`. One event cannot tell; each row records the build so that a pattern shows.

Judged unlikely: the Pi's UART or log server stuck (the node logged again right after the reset), and a dip of the Pi's 5 V (it resets the MCU, which logs a `BOOT`).
