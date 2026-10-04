"""Flash boards by Node ID: find them, build, check each chip's UID, flash both cores, confirm the boot."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Callable, Collection

from arclog.expect import DirFollower, Run, replay, spec_from_dict
from arclog.model import Kind, read_lines

from bench.boards import Board, Uid, capture_node, format_uid
from bench.build import BuildResult, config_of
from bench.pinode import PI_NODE
from bench.programmer import Programmer


def last_boot(capture_dir: Path, node: str) -> tuple[Uid | None, str | None]:
    """UID and Build ID of the last CM0+ BOOT recorded for a capture node (newest daily file first)."""
    from bench.boards import parse_uid

    for path in sorted(capture_dir.glob(f"{node}-*.log"), reverse=True):
        for line in reversed(list(read_lines(path, node=node))):
            if line.kind is Kind.ARCLOG and line.event == "BOOT" and line.core == "0" and line.get("uid"):
                try:
                    return parse_uid(line.fields["uid"]), line.get("build")
                except ValueError:
                    continue
    return None, None


def discover(prog: Programmer, ports: dict[str, str], table: dict[Uid, int], capture_dir: Path,
             probe_unknown: bool | Collection[str] = False,
             may_probe: Callable[[str], bool] | None = None) -> list[Board]:
    """Every connected probe as a Board. The UID comes from the capture's last BOOT on the probe's
    port; with probe_unknown, boards without one are read over SWD (an ST-LINK read reboots them).
    probe_unknown is True for every such board, or the ids (probe serial numbers, Pi Node names) of
    the only boards to read: a Pi Node is a debug session on a shared board. may_probe(id) is asked
    before each read and says no for a board another session holds."""
    boards = []
    for probe in prog.probes():
        b = Board(probe.sn, ports.get(probe.sn), remote=probe.board == PI_NODE)
        if b.port:
            b.uid, b.build = last_boot(capture_dir, capture_node(b.port))
            b.uid_source = "trace" if b.uid else ""
        wanted = probe_unknown if isinstance(probe_unknown, bool) else probe.sn in probe_unknown
        if b.uid is None and wanted and (may_probe is None or may_probe(probe.sn)):
            b.uid, b.uid_source = prog.read_uid(probe.sn), "swd"
        b.node_id = table.get(b.uid) if b.uid else None
        boards.append(b)
    return boards


def select(boards: list[Board], assignments: dict[int, str]) -> dict[int, Board]:
    """The board of each assigned Node ID."""
    if not boards:
        raise LookupError("no ST-LINK probe is connected")
    by_id = {b.node_id: b for b in boards if b.node_id is not None}
    missing = sorted(set(assignments) - set(by_id))
    if missing:
        unknown = [b.sn for b in boards if b.node_id is None]
        raise LookupError(f"no connected board has Node ID {', '.join(map(str, missing))}"
                          + (f"; boards with an unknown UID: {', '.join(unknown)} (try --probe-uids)"
                             if unknown else ""))
    return {nid: by_id[nid] for nid in assignments}


@dataclass
class FlashResult:
    build: BuildResult
    flashed_at: datetime | None = None
    boards: dict[int, Board] = field(default_factory=dict)
    verdict: str = ""
    ok: bool = False


def boot_check(capture_dir: Path, since: datetime, build_id: str, boards: dict[int, Board],
               assignments: dict[int, str], flashed: dict[int, datetime] | None = None, timeout_s: float = 60,
               now: Callable[[], datetime] = lambda: datetime.now(timezone.utc),
               sleep: Callable[[float], None] = __import__("time").sleep,
               report: Callable[[str], None] = print) -> tuple[bool, str]:
    """Wait for both cores of every flashed board to BOOT the build, linked, as its class, with no lost line."""
    spec = spec_from_dict({
        "timeout": timeout_s, "build": build_id, "smoke_window": timeout_s, "min_duration": 0,
        "nodes": {b.node: {"cls": assignments[nid],
                           **({"since": flashed[nid].isoformat()} if flashed and nid in flashed else {})}
                  for nid, b in boards.items()},
    })
    run = Run(spec, since, report=report)
    follower = DirFollower(capture_dir, [b.node for b in boards.values()], since)
    while True:
        replay(run, follower.poll())
        verdict = run.tick(now())
        if verdict is not None:
            return verdict.code == 0, f"{verdict.label} {verdict.reason}"
        sleep(1.0)


def flash_nodes(prog: Programmer, boards: dict[int, Board], table: dict[Uid, int],
                build: BuildResult, assignments: dict[int, str],
                report: Callable[[str], None] = print,
                now: Callable[[], datetime] = lambda: datetime.now(timezone.utc),
                ) -> tuple[datetime, dict[int, datetime]]:
    """Check every board's UID over SWD, then flash both cores of each board's configuration.

    Returns the run's start (just before the first UID read) and, per Node ID, the time its
    flash began: its lines before that belong to the previous image (or to the reboot of the
    UID read, which may be the same build when a board is reflashed). The new image's first lines
    may come before the programmer call returns, so the time is not taken after it."""
    expected = {nid: uid for uid, nid in table.items()}
    started = now() - timedelta(seconds=1)
    for nid, board in boards.items():
        uid = prog.read_uid(board.sn)
        if uid != expected[nid]:
            raise LookupError(f"probe {board.sn} ({board.port}) holds UID {format_uid(uid)}, "
                              f"not Node ID {nid} ({format_uid(expected[nid])}): nothing flashed")
    flashed: dict[int, datetime] = {}
    for nid, board in boards.items():
        cfg = config_of(assignments[nid])
        report(f"flash Node ID {nid} ({board.port}, probe {board.sn}) as {assignments[nid]}, build {build.build_id}")
        # Taken before the call: a Pi Node resets the board inside its session, which ends seconds later.
        flashed[nid] = now()
        prog.flash(board.sn, {core: build.elfs[(core, cfg)] for core in ("CM4", "CM0PLUS")})
    return started, flashed
