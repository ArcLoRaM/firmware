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
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)
