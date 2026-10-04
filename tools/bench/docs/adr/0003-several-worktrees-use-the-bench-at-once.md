# Several worktrees use the bench at once

## Status

Accepted (2026-10-04) and built on `feat/78-bench-remote-nodes`.
It replaces the part of ADR-0001 that says "no board leases, the agent owns the bench" and keeps one Build Tree and one capture for the whole machine.

## Context

ADR-0001 assumed one agent at a time on the bench.
Since then several agents work in parallel worktrees, each running `bench`, and the bench is the shared resource.
On 2026-10-04 a 60-minute acceptance run (#82) in one worktree made the bench unusable from another:

- its capture held the local COM ports, so a second `bench flash` was refused until it used `--replace`, which would have cut the first run's log and failed its Smoke Check;
- the Build Tree under `C:\Users\Simon\arcfw-bench` is rewritten by every build, whatever worktree the sources come from;
- nothing stopped two sessions from flashing or resetting the same board, which a Pi Node (a TCP port, not an exclusive COM port) makes possible.

## Decisions

**A board has one writer at a time, held by a lease.**
A command that flashes a board, resets it, or reads its UID over SWD holds that board's lease until it ends.
A run also holds a shared lease on each board it only watches: any number of runs may watch a board, none may flash it meanwhile.
A lease is a file lock in the repository's git folder (`.git/bench-leases/<board id>.lease`, shared by every worktree and never committed), so the operating system releases it when the holder ends, however it ends.
Nothing goes stale and nobody bookkeeps, which is what ADR-0001 rejected leases for.
Taking is never blocking: a board another session holds is refused at once, with who holds it (worktree, command, pid, since), and the caller picks other boards or waits.
`bench boards` shows `HELD by ...` for those boards, and a UID read skips them.
Reading a board's trace is never leased.

**One capture for the machine, shared.**
The always-on capture and the run records live in the main checkout's `tools/arclog/runs/`, found through git's common folder, so every session reads every board's trace whoever started the capture, and a record outlives its worktree.
A worktree reaches them through a `tools/arclog/runs` symlink that `bench` makes (ignored by git).
A capture that another worktree's session started (an older one, or one in its own folder) and that holds only ports this command does not use is left alone: the command records the boards it works with, says which ports stay with the other capture, and `--replace` is only for a port it needs.

**Each worktree builds in its own Build Tree.**
The default root is `C:\Users\Simon\arcfw-bench` for the main checkout and `arcfw-bench-<worktree>` for a worktree; `--root` and `$BENCH_ROOT` still override.
The first build in a new worktree is a full one.

## Considered Options

**One lock for the whole bench, sessions taking turns.**
Rejected: a 60-minute run on two boards would keep every other board idle, and an agent waiting for it has nothing to do.

**Leases kept by hand or by a scheduler daemon.**
Rejected: bookkeeping for a human, or a process to keep alive.
A file lock held by the command itself needs neither.

**A capture per worktree.**
Rejected: a COM port has one reader, so every second session needs `--replace` and the captures take the ports from each other.

## Consequences

- A session on code from before this change takes no leases and keeps its own capture folder: its boards show as free to others.
  What still protects its local boards is that a COM port has one reader, so the newer session's command is refused for any board that capture holds.
- A Pi Node has one operator at a time, as a local board has: whoever uses it, Simon or the collaborator, agrees that off-line.
  Its lease only holds back other `bench` commands; it cannot stop someone using the same OpenOCD port with CubeIDE or GDB.
- Windows tools on different probes run concurrently without a problem, and so do two CubeIDE headless builds in different workspaces.
- `tools/arclog/runs` in a worktree is a symlink to the main checkout's folder; a folder that already holds files there is left as it is.
