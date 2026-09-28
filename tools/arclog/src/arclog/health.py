"""Trace health: lost lines (sequence gaps), RTC jumps, schema problems."""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass, field
from datetime import timedelta

from arclog.model import Kind, Line
from arclog.schema import validate

#: A backwards step of the 8-bit sequence up to this size is treated as
#: reordering (a line emitted from an ISR between another line's sequence
#: number and its FIFO write), not as 250+ lost lines.
REORDER_WINDOW = 16

#: Device-time step that counts as an RTC jump between consecutive lines.
RTC_JUMP = timedelta(seconds=2)


@dataclass
class SeqResult:
    lost: int = 0          # lines lost before this one
    reordered: bool = False
    duplicate: bool = False


class SeqTracker:
    """Per (node, core) sequence-gap detector."""

    def __init__(self) -> None:
        self._last: dict[tuple[str, str], int] = {}

    def feed(self, line: Line) -> SeqResult:
        if line.kind is not Kind.ARCLOG:
            return SeqResult()
        key = (line.node, line.core)
        last = self._last.get(key)
        if line.event == "BOOT" or last is None:
            self._last[key] = line.seq
            return SeqResult()
        step = (line.seq - last) % 256
        if step == 0:
            return SeqResult(duplicate=True)
        if step > 256 - REORDER_WINDOW:
            return SeqResult(reordered=True)  # keep _last: the stream is ahead
        self._last[key] = line.seq
        return SeqResult(lost=step - 1)


@dataclass
class Health:
    lines: int = 0
    kinds: Counter = field(default_factory=Counter)
    lost: Counter = field(default_factory=Counter)        # (node, core) -> lost lines
    reordered: int = 0
    rtc_jumps: list[tuple[Line, timedelta]] = field(default_factory=list)
    problems: Counter = field(default_factory=Counter)    # schema problem -> count
    boots: Counter = field(default_factory=Counter)       # (node, core) -> BOOT count
    node_ids: set[str] = field(default_factory=set)       # Node IDs from CM0+ BOOT lines


def check(lines: list[Line]) -> Health:
    """Health summary of one node's lines (in capture order)."""
    h = Health()
    seq = SeqTracker()
    last = {}  # node -> previous line with both host and device time
    for line in lines:
        h.lines += 1
        h.kinds[line.kind] += 1
        r = seq.feed(line)
        if r.lost:
            h.lost[(line.node, line.core)] += r.lost
        if r.reordered:
            h.reordered += 1
        for p in validate(line):
            h.problems[p] += 1
        if line.kind is Kind.ARCLOG and line.event == "BOOT":
            h.boots[(line.node, line.core)] += 1
            if "id" in line.fields:
                h.node_ids.add(line.fields["id"])
        if line.dev_time is not None and line.host_time is not None:
            prev = last.get(line.node)
            if prev is not None and line.event != "BOOT":
                # Device clock step not explained by the host clock step.
                jump = (line.dev_time - prev.dev_time) - (line.host_time - prev.host_time)
                if jump > RTC_JUMP or jump < -RTC_JUMP:
                    # Expected after RTC_SET; still reported so it is visible.
                    h.rtc_jumps.append((line, jump))
            last[line.node] = line
    return h
