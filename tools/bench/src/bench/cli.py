"""bench command line."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

from bench.tree import BuildTree

DEFAULT_ROOT = "/mnt/c/Users/Simon/arcfw-bench"


def find_repo(start: Path) -> Path:
    """The firmware repo containing `start` (the directory holding ArcLoRaM_Base.ioc)."""
    top = subprocess.run(["git", "-C", str(start), "rev-parse", "--show-toplevel"],
                         check=True, capture_output=True, text=True).stdout.strip()
    repo = Path(top)
    if not (repo / "ArcLoRaM_Base.ioc").exists():
        raise ValueError(f"{repo} is not the firmware repo (no ArcLoRaM_Base.ioc)")
    return repo


def parse_define(text: str) -> tuple[str, str]:
    name, sep, value = text.partition("=")
    if not sep:
        raise argparse.ArgumentTypeError(f"-D {text}: expected NAME=VALUE")
    return name, value


def _windows(path: Path) -> str:
    """Windows form of a Build Tree path (the tests use a local directory as is)."""
    from bench.winpath import to_windows

    return to_windows(path) if str(path).startswith("/mnt/") else str(path)


def cmd_build(args: argparse.Namespace) -> int:
    from bench.build import build

    repo = Path(args.repo) if args.repo else find_repo(Path.cwd())
    bt = BuildTree(Path(args.root))
    overrides = dict(args.define or [])
    try:
        result = build(repo, bt, args.cls, overrides, windows=_windows, clean=args.clean)
    except (ValueError, subprocess.CalledProcessError) as exc:
        print(f"bench build: {exc}", file=sys.stderr)
        return 2

    logs = bt.root / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    log_path = logs / f"build-{result.build_id}.log"
    log_path.write_text(result.raw_log, encoding="utf-8")

    for d in result.log.errors:
        print(d)
    for d in result.log.warnings:
        print(d)
    print(f"build {result.build_id} ({', '.join(result.configs)}): "
          f"{'ok' if result.ok else 'FAILED'}, {len(result.log.errors)} error(s), "
          f"{len(result.log.warnings)} warning(s); log {log_path}")
    for problem in result.problems:
        print(f"  {problem}")
    if result.ok:
        for (core, cfg), elf in sorted(result.elfs.items()):
            print(f"  {cfg} {core}: {elf}")
    return 0 if result.ok else 1


def parse_assignment(text: str) -> tuple[int, str]:
    """'2=C2' -> (2, 'C2'): Node ID and the class it is flashed as."""
    nid, sep, cls = text.partition("=")
    if not sep or not nid.isdigit() or cls not in ("C1", "C2", "C3"):
        raise argparse.ArgumentTypeError(f"--node {text}: expected <Node ID>=C1|C2|C3, e.g. 2=C2")
    return int(nid), cls


def _context(args: argparse.Namespace):
    from bench.boards import load_node_table, query_ports
    from bench.capture import CAPTURE_DIR
    from bench.programmer import Programmer

    repo = Path(args.repo) if args.repo else find_repo(Path.cwd())
    return repo, Programmer(), query_ports(), load_node_table(repo), repo / CAPTURE_DIR


def cmd_boards(args: argparse.Namespace) -> int:
    from bench.boards import format_uid
    from bench.flash import discover

    repo, prog, ports, table, capture_dir = _context(args)
    boards = discover(prog, ports, table, capture_dir, probe_unknown=args.probe_uids)
    for b in boards:
        uid = f"{format_uid(b.uid)} ({b.uid_source})" if b.uid else "unknown (no BOOT in the capture)"
        nid = b.node_id if b.node_id is not None else ("not in node_id.c" if b.uid else "?")
        build = b.build or ("none (firmware without build= in BOOT)" if b.uid_source == "trace" else "?")
        print(f"probe {b.sn}  {b.port or 'no COM port'}  Node ID {nid}  UID {uid}  last build {build}")
    return 0


def cmd_capture(args: argparse.Namespace) -> int:
    from bench.capture import Capture

    repo, prog, ports, _, _ = _context(args)
    present = [ports[p.sn] for p in prog.probes() if p.sn in ports]
    cap = Capture(repo)
    if args.action == "status":
        st = cap.status(present)
        if st.running:
            print(f"bench capture: pid {st.running.pid}, ports {', '.join(st.running.ports)} -> {cap.dir}")
        else:
            print("bench capture: not running")
        for p in st.foreign:
            print(f"other capture: pid {p.pid}, ports {', '.join(p.ports)} -> {p.out}")
        if st.missing:
            print(f"not recorded: {', '.join(st.missing)} (bench capture up)")
        return 0 if st.running and not st.missing else 1
    try:
        _, done = cap.up(present, replace=args.replace)
    except RuntimeError as exc:
        print(f"bench capture: {exc}", file=sys.stderr)
        return 1
    print(f"bench capture {done}: {', '.join(present)} -> {cap.dir}")
    return 0


def cmd_flash(args: argparse.Namespace) -> int:
    from bench.build import build
    from bench.capture import Capture
    from bench.flash import boot_check, discover, flash_nodes, select
    from bench.programmer import ProgrammerError, Refused

    assignments = dict(args.node)
    repo, prog, ports, table, capture_dir = _context(args)
    try:
        boards = select(discover(prog, ports, table, capture_dir, probe_unknown=args.probe_uids), assignments)
        present = [ports[p.sn] for p in prog.probes() if p.sn in ports]
        _, done = Capture(repo).up(present)
        print(f"capture {done}: {', '.join(present)}")
        result = build(repo, BuildTree(Path(args.root)), sorted(set(assignments.values())),
                       dict(args.define or []), windows=_windows)
        if not result.ok:
            for d in result.log.errors:
                print(d)
            print(f"build {result.build_id} FAILED: {'; '.join(result.problems)}")
            return 1
        print(f"build {result.build_id} ok")
        since, flashed = flash_nodes(prog, boards, table, result, assignments)
    except (LookupError, RuntimeError, ValueError, Refused, ProgrammerError) as exc:
        print(f"bench flash: {exc}", file=sys.stderr)
        return 1
    if args.no_check:
        return 0
    ok, verdict = boot_check(capture_dir, since, result.build_id, boards, assignments, flashed,
                             timeout_s=args.timeout)
    print(f"boot check: {verdict}")
    return 0 if ok else 1


def cmd_reset(args: argparse.Namespace) -> int:
    from bench.flash import discover, select
    from bench.programmer import ProgrammerError

    repo, prog, ports, table, capture_dir = _context(args)
    try:
        boards = select(discover(prog, ports, table, capture_dir), {nid: "" for nid in args.node_ids})
        for nid, b in boards.items():
            prog.reset(b.sn)
            print(f"reset Node ID {nid} ({b.port}, probe {b.sn})")
    except (LookupError, ProgrammerError) as exc:
        print(f"bench reset: {exc}", file=sys.stderr)
        return 1
    return 0


def _scenario_from_args(args: argparse.Namespace):
    """(Scenario, TOML text) from a file, or from --node/--watch/--expect/--forbid/-D/--timeout."""
    from bench.scenario import from_dict, load, parse_expect_text, to_toml

    if args.scenario:
        if args.node or args.watch or args.expect or args.forbid or args.define:
            raise ValueError("give a scenario file or --node/--watch/--expect/--forbid/-D, not both")
        return load(args.scenario), Path(args.scenario).read_text(encoding="utf-8")
    if not args.node:
        raise ValueError("give a scenario file or at least one --node ID=CLASS")
    d: dict = {}
    if args.description:
        d["description"] = args.description
    d["timeout"] = args.timeout or "10m"
    d["nodes"] = {str(nid): cls for nid, cls in args.node}
    d["nodes"].update({str(nid): "watch" for nid in args.watch or []})
    if args.define:
        d["overrides"] = dict(args.define)
    d["expect"] = [parse_expect_text(e) for e in args.expect or []]
    d["forbid"] = [{"event": ev} for ev in args.forbid or []]
    text = to_toml(d)
    return from_dict(d), text


def cmd_run(args: argparse.Namespace) -> int:
    from datetime import datetime, timezone

    from bench.build import build
    from bench.capture import Capture
    from bench.flash import discover, flash_nodes, select
    from bench.programmer import ProgrammerError, Refused
    from bench.run import follow, run_dir, slice_capture, write_record
    from bench.scenario import plan

    try:
        s, text = _scenario_from_args(args)
    except ValueError as exc:
        print(f"bench run: {exc}", file=sys.stderr)
        return 3
    if args.save:
        Path(args.save).write_text(text, encoding="utf-8")
        print(f"scenario saved to {args.save}")
    print(plan(s))
    repo, prog, ports, table, capture_dir = _context(args)
    try:
        found = discover(prog, ports, table, capture_dir, probe_unknown=args.probe_uids)
        boards = select(found, s.nodes)
        others = [b for b in found if b not in boards.values()]
        present = [ports[p.sn] for p in prog.probes() if p.sn in ports]
        _, done = Capture(repo).up(present)
        print(f"capture {done}: {', '.join(present)}")
        result = build(repo, BuildTree(Path(args.root)), sorted(set(s.flashed.values())), s.overrides,
                       windows=_windows)
        if not result.ok:
            for d in result.log.errors:
                print(d)
            print(f"build {result.build_id} FAILED: {'; '.join(result.problems)}")
            return 1
        print(f"build {result.build_id} ok")
        since, flashed = flash_nodes(prog, {n: boards[n] for n in s.flashed}, table, result, s.flashed)
        outcome = follow(s, boards, result.build_id, capture_dir, since, reset=prog.reset, flashed=flashed)
    except (LookupError, RuntimeError, ValueError, Refused, ProgrammerError) as exc:
        print(f"bench run: {exc}", file=sys.stderr)
        return 1
    out = run_dir(repo / "tools/arclog/runs", since, result.build_id)
    slice_capture(capture_dir, out, [b.node for b in boards.values()], since, outcome.ended)
    report = write_record(out, text, s, result.build_id, outcome, boards, others)
    print(f"{outcome.verdict}\nrecord: {report}")
    return outcome.code


def cmd_scenario(args: argparse.Namespace) -> int:
    from bench.scenario import load, plan

    try:
        s = load(args.file)
    except (OSError, ValueError) as exc:
        print(f"{args.file}: {exc}", file=sys.stderr)
        return 3
    print(plan(s))
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="bench", description=__doc__)
    p.add_argument("--root", default=os.environ.get("BENCH_ROOT", DEFAULT_ROOT),
                   help=f"bench directory on the Windows disk (default: {DEFAULT_ROOT})")
    p.add_argument("--repo", help="firmware repo (default: the git repo of the current directory)")
    sub = p.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("build", help="sync the Build Tree and build both cores headless; exit 0 ok, 1 failed")
    b.add_argument("--class", dest="cls", action="append", required=True, choices=["C1", "C2", "C3"],
                   help="node class to build (both cores of Debug_<class>); repeat for several")
    b.add_argument("-D", dest="define", action="append", type=parse_define, metavar="NAME=VALUE",
                   help="Build Override for this build only; repeat for several")
    b.add_argument("--clean", action="store_true", help="rebuild everything instead of an incremental build")
    b.set_defaults(func=cmd_build)

    bd = sub.add_parser("boards", help="list the connected boards: probe, COM port, UID, Node ID, last build")
    bd.add_argument("--probe-uids", action="store_true",
                    help="read unknown UIDs over SWD (reboots those boards)")
    bd.set_defaults(func=cmd_boards)

    c = sub.add_parser("capture", help="the always-on capture of every board's trace")
    c.add_argument("action", choices=["up", "status"])
    c.add_argument("--replace", action="store_true", help="stop another capture holding the ports")
    c.set_defaults(func=cmd_capture)

    f = sub.add_parser("flash", help="build, then flash both cores of each named board and check its boot; "
                                     "exit 0 ok, 1 failed")
    f.add_argument("--node", action="append", required=True, type=parse_assignment, metavar="ID=CLASS",
                   help="Node ID and the class to flash it as, e.g. 2=C2; repeat for several")
    f.add_argument("-D", dest="define", action="append", type=parse_define, metavar="NAME=VALUE",
                   help="Build Override for this build only")
    f.add_argument("--probe-uids", action="store_true", help="read unknown UIDs over SWD (reboots those boards)")
    f.add_argument("--timeout", type=float, default=60, help="seconds to wait for the boot check")
    f.add_argument("--no-check", action="store_true", help="do not wait for the boot")
    f.set_defaults(func=cmd_flash)

    rn = sub.add_parser("run", help="run a scenario: build, flash, act, decide, record; "
                                    "exit 0 pass, 1 fail, 2 timeout, 3 invalid scenario")
    rn.add_argument("scenario", nargs="?", help="scenario file (TOML); or describe the run with the options")
    rn.add_argument("--node", action="append", type=parse_assignment, metavar="ID=CLASS",
                    help="flash Node ID as C1/C2/C3, e.g. 2=C2; repeat for several")
    rn.add_argument("--watch", action="append", type=int, metavar="ID",
                    help="check this Node ID's trace without flashing it")
    rn.add_argument("--expect", action="append", metavar="'ID|any EVENT [field=value] [within=7m] [count=2]'",
                    help="e.g. '2 CLK to=WARM within=7m'; repeat for several")
    rn.add_argument("--forbid", action="append", metavar="EVENT", help="event that fails the run, e.g. TX_LATE")
    rn.add_argument("-D", dest="define", action="append", type=parse_define, metavar="NAME=VALUE",
                    help="Build Override for this run only")
    rn.add_argument("--timeout", help="e.g. 10m (default 10m)")
    rn.add_argument("--description", help="one line saved with the scenario")
    rn.add_argument("--save", metavar="FILE", help="also save the scenario described by the options")
    rn.add_argument("--probe-uids", action="store_true", help="read unknown UIDs over SWD (reboots those boards)")
    rn.set_defaults(func=cmd_run)

    sc = sub.add_parser("scenario", help="check a scenario file without touching the boards")
    sc.add_argument("action", choices=["check"])
    sc.add_argument("file")
    sc.set_defaults(func=cmd_scenario)

    r = sub.add_parser("reset", help="reset boards (no flash)")
    r.add_argument("node_ids", nargs="+", type=int, metavar="NODE_ID")
    r.set_defaults(func=cmd_reset)
    return p


def main(argv: list[str] | None = None) -> int:
    # Progress must reach a pipe (Monitor, tee) as it happens, not when the run ends.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    args = build_parser().parse_args(argv)
    return args.func(args)
