"""Validity of a capture session: what makes its data unfit to use, from the capture's own marks.

A cause is something the bench did wrong (a port down, a host that was not running, a clock step), not a
verdict on the firmware: a node that is silent while its port is up is not found here.
Other evidence (the log collector) produces causes of the same shape; the caller joins them.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime

from arclog.marks import Mark

#: A port down for longer than this is a capture gap: a running node logs a line every few seconds.
PORT_DOWN_S = 10.0

#: UTC against the monotonic clock between two marks: a time service slews well below this, a step does not.
UTC_STEP_S = 1.0

#: Marks come every 10 s: none for longer than this means the capture, or the host, was not running.
NO_MARKS_S = 30.0


@dataclass(frozen=True)
class Cause:
    kind: str                       # port_down, utc_step, no_marks
    text: str                       # one line for a person, naming the node, interval or size
    node: str | None = None
    start: datetime | None = None
    end: datetime | None = None


def check_marks(marks: list[Mark], start: datetime, end: datetime) -> list[Cause]:
    """The causes that make the session between `start` and `end` (UTC) unfit, from the marks of its capture."""
    marks = [m for m in marks if start <= m.utc <= end]
    if not marks:
        return _uncovered(start, end)
    causes = _uncovered(start, marks[0].utc) + _uncovered(marks[-1].utc, end)
    for node, down_from, down_to in _down_intervals(marks):
        down_from = max(down_from, start)
        seconds = (down_to - down_from).total_seconds()
        if seconds > PORT_DOWN_S:
            causes.append(Cause("port_down", f"{node}: port down for {seconds:.0f} s "
                                f"({down_from:%H:%M:%S} - {down_to:%H:%M:%S} UTC)", node, down_from, down_to))
    for before, after in zip(marks, marks[1:]):
        elapsed = (after.utc - before.utc).total_seconds()
        silent = max(elapsed, after.mono - before.mono)  # a suspended host's monotonic clock may not count its sleep
        if silent > NO_MARKS_S:
            causes.append(_no_marks(before.utc, after.utc, silent))
            continue
        step = elapsed - (after.mono - before.mono)
        if abs(step) > UTC_STEP_S:
            causes.append(Cause("utc_step", f"the host's UTC was stepped {step:+.1f} s "
                                f"({before.utc:%H:%M:%S} - {after.utc:%H:%M:%S} UTC)", None, before.utc, after.utc))
    return causes


def _uncovered(before: datetime, after: datetime) -> list[Cause]:
    """A stretch with no mark at all (the edges of the window, or the whole of it)."""
    seconds = (after - before).total_seconds()
    return [_no_marks(before, after, seconds)] if seconds > NO_MARKS_S else []


def _no_marks(before: datetime, after: datetime, seconds: float) -> Cause:
    return Cause("no_marks", f"no marks for {seconds:.0f} s: the capture or the host was not running "
                 f"({before:%H:%M:%S} - {after:%H:%M:%S} UTC)", None, before, after)


def _down_intervals(marks: list[Mark]) -> list[tuple[str, datetime, datetime]]:
    """(node, down since, up again) for every outage the marks show; one still on at the last mark ends there."""
    down: dict[str, datetime] = {}
    outages = []
    for m in marks:
        for node, port in m.ports.items():
            if not port.up:
                down.setdefault(node, port.since)
            elif node in down:
                outages.append((node, down.pop(node), port.since))
    outages += [(node, since, marks[-1].utc) for node, since in down.items()]
    return outages
