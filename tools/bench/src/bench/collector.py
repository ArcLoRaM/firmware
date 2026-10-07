"""Decide a scenario from the log collector's lines, so a long run does not need the host.

The collector is a server that stores every Pi Node's UART log.
`bench flash` arms the boards in a few minutes; the host can then be switched off, the collector keeps
recording, and this module turns its lines into capture files and decides the scenario with the same
engine and the same expect file as `bench run` (`expect_spec`, arclog's `Run`).

    python -m bench.collector scenarios/x.toml --collector http://HOST:PORT --build ID \\
        --since 2026-10-07T08:40:00Z --node 2=nuna-node-03 --node 5=nuna-node-01

Exit code: 0 pass, 1 fail, 2 no verdict yet (nothing failed so far, or the run timed out undecided).

The collector writes each line's time in ISO 8601 with its offset; its first version wrote the Pi's local time
without a zone (`2026-10-07 04:28:49.615240`), which `--pi-tz` still converts.
A gap in the collector's stream (a restart, a dropped connection, more than the Pi buffered) shows as lost
lines and fails the run: such a run is invalid, not a verdict on the firmware.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.request
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone, tzinfo
from pathlib import Path
from types import SimpleNamespace
from zoneinfo import ZoneInfo

from arclog.expect import DirFollower, Run, parse_since, replay, spec_from_dict
from arclog.model import Line, format_host_time

from bench.run import expect_spec
from bench.scenario import Scenario, load

# <time> | <node> | <device line>; the time is ISO 8601 with its offset (2026-10-07T16:03:58.648+02:00),
# or, from the first version of the collector, the Pi's local time with no zone (2026-10-07 04:28:49.615).
_LINE = re.compile(r"^(\d{4}-\d\d-\d\d)[ T](\d\d:\d\d:\d\d(?:\.\d+)?)(Z|[+-]\d\d:\d\d)? \| (\S+) \| (.*)$")
# The Pi's own lines in the stream ("[pi] uart_idle=6s ntp=synced"): not trace lines.
_PI_LINE = "[pi] "

# "ok   nuna-node-03 SLOT (12/150) +215.4s": an expectation that counts, 12 of 150 so far.
_PROGRESS = re.compile(r"^ok\s+\S+ \S+ \((\d+)/(\d+)\)")

# The run is armed by the BOOTs after `bench flash`, minutes after `since`.
SMOKE_WINDOW = "15m"
# Clock offsets beyond this between a Pi and the collector shift every time of the run.
CLOCK_SKEW_S = 5.0
# A node that logs a slot every 20 s and has been quiet this long has stopped.
SILENT_WARN_S = 600.0


def convert_line(text: str, pi_tz: tzinfo) -> tuple[str, datetime, str] | None:
    """(node, UTC time, device line) of one collector line, or None for a line of another shape
    or one of the Pi's own."""
    m = _LINE.match(text.rstrip("\r\n"))
    if m is None:
        return None
    day, clock, offset, node, device = m.groups()
    if device.startswith(_PI_LINE):
        return None
    when = datetime.fromisoformat(f"{day}T{clock}{'+00:00' if offset == 'Z' else offset or ''}")
    if offset is None:
        when = when.replace(tzinfo=pi_tz)
    return node, when.astimezone(timezone.utc), device


def write_capture(text: str, out_dir: Path, pi_tz: tzinfo) -> dict[str, int]:
    """Write the collector's lines as capture files (<node>-YYYYMMDD.log, UTC days); lines per node."""
    files: dict[tuple[str, str], list[str]] = defaultdict(list)
    for raw in text.splitlines():
        converted = convert_line(raw, pi_tz)
        if converted is not None:
            node, t, device = converted
            files[(node, t.strftime("%Y%m%d"))].append(f"{format_host_time(t)}\t{device}\n")
    out_dir.mkdir(parents=True, exist_ok=True)
    counts: Counter[str] = Counter()
    for (node, day), lines in files.items():
        (out_dir / f"{node}-{day}.log").write_text("".join(lines), encoding="utf-8")
        counts[node] += len(lines)
    return dict(counts)


def node_number(name: str) -> int:
    """The collector's number of a node: nuna-node-03 is 3."""
    m = re.search(r"(\d+)$", name)
    if m is None:
        raise ValueError(f"{name!r} does not end with a node number")
    return int(m[1])


def http_get(url: str, timeout: float = 120.0) -> str:
    # The collector speaks plain http on the tailnet.
    with urllib.request.urlopen(url, timeout=timeout) as response:  # noqa: S310
        return response.read().decode("utf-8", errors="replace")


def clock_warnings(nodes: dict) -> list[str]:
    """A Pi whose clock is not synchronised, or lags the collector's, shifts every time of the run."""
    out = []
    for name, n in sorted(nodes.items()):
        if n.get("pi_ntp") not in (None, "synced"):
            out.append(f"{name}: the Pi's clock is not synchronised ({n['pi_ntp']}): times are shifted")
        lag = n.get("clock_lag_s")
        if lag is not None and abs(lag) > CLOCK_SKEW_S:
            out.append(f"{name}: its clock lags the collector's by {lag:+.1f} s: times are shifted")
    return out


def validity_warnings(names: list[str], nodes: dict, since: datetime) -> list[str]:
    """What makes the collector's record unfit to decide the run, whatever the firmware did."""
    out = []
    for name in names:
        n = nodes.get(name)
        if n is None:
            out.append(f"{name}: the collector does not know this node")
            continue
        if not n.get("connected"):
            out.append(f"{name}: the collector is not connected to it now ({n.get('last_error')})")
        started = n.get("connected_since")
        if started and datetime.fromisoformat(started) > since:
            out.append(f"{name}: the collector (re)connected at {started}, after the run started "
                       f"(the Pi replayed {n.get('recovered_from_replay', 0)} time(s) what it had buffered); "
                       "a FAIL on lost lines says whether that was enough")
        silent = n.get("silent_for_s")
        if silent is not None and silent > SILENT_WARN_S:
            out.append(f"{name}: the collector has seen no line from it for {silent:.0f} s")
    return out


def _per_hour(lines: list[Line], event: str, since: datetime) -> list[int]:
    hours = [ln for ln in lines if ln.event == event and ln.host_time is not None]
    if not hours:
        return []
    last = int((max(ln.host_time for ln in hours) - since) / timedelta(hours=1))
    counts = [0] * (last + 1)
    for ln in hours:
        counts[max(0, int((ln.host_time - since) / timedelta(hours=1)))] += 1
    return counts


def describe_nodes(names: list[str], lines: list[Line], since: datetime, now: datetime) -> list[str]:
    out = []
    for name in names:
        own = sorted((ln for ln in lines if ln.node == name and ln.host_time is not None),
                     key=lambda ln: ln.host_time)
        if not own:
            out.append(f"{name}: no line since {format_host_time(since)}")
            continue
        boots = [f"{format_host_time(ln.host_time)[11:19]} {ln.fields.get('cls', '')} {ln.fields.get('build', '')}"
                 for ln in own if ln.event == "BOOT" and ln.core == "0"]
        silent = now - own[-1].host_time
        out.append(f"{name}: {len(own)} lines, first {format_host_time(own[0].host_time)[11:19]}Z, "
                   f"last {format_host_time(own[-1].host_time)[11:19]}Z ({silent.total_seconds() / 60:.0f} min ago), "
                   f"CM0+ BOOT {'; '.join(boots) or 'none'}")
        out.append(f"    SLOT per hour from the start: {_per_hour(own, 'SLOT', since)}")
    return out


def judge(s: Scenario, build: str, names: dict[int, str], since: datetime, base: str, out_dir: Path,
          pi_tz: tzinfo, flashed: dict[int, datetime] | None = None,
          now: datetime | None = None, report=print) -> int:
    """Fetch the collector's record of the run, decide the scenario on it, report; the exit code."""
    now = now or datetime.now(timezone.utc)
    nodes = {n: names[n] for n in s.nodes}
    boards = {nid: SimpleNamespace(node=name) for nid, name in nodes.items()}
    numbers = ",".join(str(node_number(name)) for name in nodes.values())
    start = (since - timedelta(minutes=5)).astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    text = http_get(f"{base}/logs?nodes={numbers}&since={start}")
    status = json.loads(http_get(f"{base}/nodes"))
    write_capture(text, out_dir, pi_tz)

    d = expect_spec(s, boards, build, flashed)
    d["smoke_window"] = SMOKE_WINDOW
    spec = spec_from_dict(d)

    def say(msg: str) -> None:
        # A run of hours counts every slot; only the count that completes an expectation is news.
        m = _PROGRESS.match(msg)
        if m is None or m[1] == m[2]:
            report(msg)

    run = Run(spec, since, report=say)
    lines = DirFollower(out_dir, list(spec.nodes), since).poll()
    verdict = replay(run, lines) or run.tick(now)

    names_list = list(nodes.values())
    for line in describe_nodes(names_list, lines, since, now):
        report(line)
    for warning in clock_warnings(status) + validity_warnings(names_list, status, since):
        report(f"WARN {warning}")
    if verdict is None:
        report(f"NO VERDICT YET at {format_host_time(now)}: nothing has failed so far")
        return 2
    return verdict.code


def _pairs(values: list[str], what: str) -> dict[int, str]:
    out = {}
    for v in values:
        key, sep, val = v.partition("=")
        if not sep or not key.isdigit():
            raise ValueError(f"{what} {v!r}: use ID=VALUE")
        out[int(key)] = val
    return out


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="python -m bench.collector", description=__doc__.split("\n\n")[0])
    p.add_argument("scenario", help="scenario file (TOML)")
    p.add_argument("--collector", default=os.environ.get("BENCH_COLLECTOR_URL"),
                   help="base URL of the log collector (default: $BENCH_COLLECTOR_URL)")
    p.add_argument("--build", required=True, help="Build ID the flashed boards boot (bench flash prints it)")
    p.add_argument("--since", required=True, help="ISO UTC time, before the flash")
    p.add_argument("--node", action="append", default=[], metavar="ID=NAME",
                   help="the collector's name of a Node ID's Pi Node, e.g. 2=nuna-node-03; repeat")
    p.add_argument("--flashed", action="append", default=[], metavar="ID=ISO",
                   help="when a node's flash ended, to ignore its old image's lines; repeat")
    p.add_argument("--dir", required=True, type=Path, help="where the capture files are written")
    p.add_argument("--pi-tz", default="Europe/Paris", help="the Pi's time zone (default: %(default)s)")
    args = p.parse_args(argv)
    try:
        if not args.collector:
            raise ValueError("no collector: pass --collector or set BENCH_COLLECTOR_URL")
        s = load(args.scenario)
        names = _pairs(args.node, "--node")
        missing = [nid for nid in s.nodes if nid not in names]
        if missing:
            raise ValueError(f"--node is missing for Node ID {', '.join(map(str, missing))}")
        flashed = {nid: parse_since(v) for nid, v in _pairs(args.flashed, "--flashed").items()}
        return judge(s, args.build, names, parse_since(args.since), args.collector.rstrip("/"),
                     args.dir, ZoneInfo(args.pi_tz), flashed or None)
    except (OSError, ValueError) as exc:
        print(f"bench.collector: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
