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
        since = flash_nodes(prog, boards, table, result, assignments)
    except (LookupError, RuntimeError, ValueError, Refused, ProgrammerError) as exc:
        print(f"bench flash: {exc}", file=sys.stderr)
        return 1
    if args.no_check:
        return 0
    ok, verdict = boot_check(capture_dir, since, result.build_id, boards, assignments, timeout_s=args.timeout)
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

    r = sub.add_parser("reset", help="reset boards (no flash)")
    r.add_argument("node_ids", nargs="+", type=int, metavar="NODE_ID")
    r.set_defaults(func=cmd_reset)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)
