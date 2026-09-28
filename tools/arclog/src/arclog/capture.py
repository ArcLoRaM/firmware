"""Serial capture: every received line is stored verbatim with its host UTC time.

One process captures any number of ports, one node per port. Files are named
<node>-<YYYYMMDD>.log (UTC date) inside the output directory and rotate at
UTC midnight, so a months-long run is a directory of daily files. Each port
is read by its own thread and reopened automatically when it disappears (USB
replug, board reset), independently of the others, which matters for
unattended runs. All lines are written by the calling thread, stamped with
the one host clock at receive time.
"""

from __future__ import annotations

import queue
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Iterator, TextIO

from arclog.model import Line, format_host_time, parse_line

DEFAULT_BAUD = 9600
RECONNECT_S = 2.0


def serial_lines(port: str, baud: int = DEFAULT_BAUD, reconnect: bool = True,
                 log: Callable[[str], None] = lambda m: print(m, file=sys.stderr),
                 duration_s: float | None = None,
                 ) -> Iterator[tuple[datetime, str]]:
    """Yield (host UTC time, line) from a serial port, reconnecting on errors.

    Stops after duration_s seconds when given, otherwise runs until interrupted.
    """
    import serial  # pyserial; imported here so offline commands do not need it

    deadline = time.monotonic() + duration_s if duration_s else None
    down = False  # an outage is logged once, not at every retry
    while True:
        try:
            with serial.Serial(port, baud, timeout=1.0) as ser:
                down = False
                log(f"arclog: listening on {port} @ {baud}")
                buf = bytearray()
                while True:
                    if deadline is not None and time.monotonic() >= deadline:
                        return
                    chunk = ser.read(ser.in_waiting or 1)
                    if not chunk:
                        continue
                    buf.extend(chunk)
                    while True:
                        nl = buf.find(b"\n")
                        if nl < 0:
                            break
                        raw = bytes(buf[:nl]).decode("ascii", errors="replace").rstrip("\r")
                        del buf[: nl + 1]
                        if raw.strip():
                            yield datetime.now(timezone.utc), raw
        except (serial.SerialException, OSError) as exc:
            if not reconnect:
                raise
            if deadline is not None and time.monotonic() >= deadline:
                return
            if not down:
                log(f"arclog: {port} unavailable ({exc}); retrying every {RECONNECT_S:.0f} s")
                down = True
            time.sleep(RECONNECT_S)


class DailyWriter:
    """Append '<host UTC>\\t<line>' records to <dir>/<node>-<YYYYMMDD>.log."""

    def __init__(self, out_dir: Path, node: str) -> None:
        self.out_dir = out_dir
        self.node = node
        self._date = ""
        self._file: TextIO | None = None
        out_dir.mkdir(parents=True, exist_ok=True)

    def path_for(self, t: datetime) -> Path:
        return self.out_dir / f"{self.node}-{t.strftime('%Y%m%d')}.log"

    def write(self, t: datetime, raw: str) -> None:
        date = t.strftime("%Y%m%d")
        if date != self._date:
            self.close()
            self._file = self.path_for(t).open("a", encoding="utf-8", newline="\n")
            self._date = date
        assert self._file is not None
        self._file.write(f"{format_host_time(t)}\t{raw}\n")
        self._file.flush()

    def close(self) -> None:
        if self._file is not None:
            self._file.close()
            self._file = None


LineSource = Callable[[str], Iterator[tuple[datetime, str]]]
"""Yields (host UTC time, line) for a port; serial_lines in production."""


def _reader(port: str, node: str, source: LineSource, out: queue.Queue) -> None:
    try:
        for t, raw in source(port):
            out.put((node, t, raw))
    except BaseException as exc:  # reported by the writing thread
        out.put((node, None, exc))
    finally:
        out.put((node, None, None))


def capture(ports: list[tuple[str, str]], out_dir: Path, baud: int = DEFAULT_BAUD,
            on_line: Callable[[Line], None] | None = None,
            duration_s: float | None = None,
            source: LineSource | None = None) -> None:
    """Capture (port, node) pairs until interrupted (Ctrl+C) or for duration_s seconds."""
    ports_seen = [p for p, _ in ports]
    nodes_seen = [n for _, n in ports]
    if not ports:
        raise ValueError("no port to capture")
    if len(set(ports_seen)) != len(ports_seen):
        raise ValueError(f"port given twice: {ports_seen}")
    if len(set(nodes_seen)) != len(nodes_seen):
        raise ValueError(f"node name given twice (would share a file): {nodes_seen}")
    if source is None:
        def source(port: str) -> Iterator[tuple[datetime, str]]:
            return serial_lines(port, baud, duration_s=duration_s)

    lines: queue.Queue = queue.Queue()
    writers = {node: DailyWriter(out_dir, node) for _, node in ports}
    for port, node in ports:
        threading.Thread(target=_reader, args=(port, node, source, lines),
                         name=f"arclog-{port}", daemon=True).start()
    running = len(ports)
    try:
        while running:
            try:
                # A timeout keeps Ctrl+C responsive on Windows.
                node, t, item = lines.get(timeout=0.5)
            except queue.Empty:
                continue
            if t is None:
                if isinstance(item, BaseException):
                    raise item
                running -= 1
                continue
            writers[node].write(t, item)
            if on_line is not None:
                on_line(parse_line(item, node=node, host_time=t))
    except KeyboardInterrupt:
        pass
    finally:
        for w in writers.values():
            w.close()
