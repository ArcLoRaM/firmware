"""Headless CubeIDE build of the Build Tree, with diagnostics mapped back to repo paths."""

from __future__ import annotations

import os
import posixpath
import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from bench.buildid import build_id, render_overrides_h, write_if_changed
from bench.tree import BuildTree, sync
from bench.winpath import to_windows

#: stm32cubeidec.exe, the console launcher of STM32CubeIDE (override with BENCH_CUBEIDE).
DEFAULT_CUBEIDE = "/mnt/c/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/stm32cubeidec.exe"

CORES = ("CM4", "CM0PLUS")
PROJECT = {"CM4": "ArcLoRaM_Base_CM4", "CM0PLUS": "ArcLoRaM_Base_CM0PLUS"}
CORE_OF_PROJECT = {v: k for k, v in PROJECT.items()}
CLASSES = ("C1", "C2", "C3")


def config_of(cls: str) -> str:
    """Node Class -> build configuration: 'C2' -> 'Debug_C2'."""
    if cls not in CLASSES:
        raise ValueError(f"unknown node class {cls!r} (expected one of {', '.join(CLASSES)})")
    return f"Debug_{cls}"


def headless_command(cubeide: str, bt: BuildTree, configs: list[str],
                     windows: Callable[[Path], str] = to_windows, clean: bool = False) -> list[str]:
    """Import the three projects (a no-op once imported) and build both cores of each configuration.

    clean: rebuild everything (-cleanBuild) instead of an incremental build.
    """
    cmd = [cubeide, "--launcher.suppressErrors", "-nosplash",
           "-application", "org.eclipse.cdt.managedbuilder.core.headlessbuild",
           "-data", windows(bt.workspace)]
    for sub in ("", "CM4", "CM0PLUS"):
        cmd += ["-import", windows(bt.tree / sub if sub else bt.tree)]
    for config in configs:
        for core in CORES:
            cmd += ["-cleanBuild" if clean else "-build", f"{PROJECT[core]}/{config}"]
    return cmd


# ---------------------------------------------------------------------------
# Log
# ---------------------------------------------------------------------------

_HEADER_RE = re.compile(r"\*\*\*\* Build of configuration (\S+) for project (\S+) \*\*\*\*")
_SUMMARY_RE = re.compile(r"Build (Finished|Failed)\. (\d+) errors?, (\d+) warnings?\.")
_DIAG_RE = re.compile(r"^(?P<file>(?:[A-Za-z]:)?[^:\s][^:]*):(?P<line>\d+):(?:(?P<col>\d+):)? "
                      r"(?P<sev>error|warning|fatal error): (?P<msg>.*)$")
_LINKER_RE = re.compile(r"(?:^|[/\\])ld(?:\.exe)?: (?P<sev>warning|error): (?P<msg>.*)$")


@dataclass
class Diagnostic:
    severity: str        # error / warning
    message: str
    file: str = ""       # repo-relative path, "" for linker messages
    line: int = 0
    col: int = 0
    core: str = ""
    config: str = ""

    def __str__(self) -> str:
        where = f"{self.file}:{self.line}:{self.col}: " if self.file else "ld: "
        return f"{where}{self.severity}: {self.message} [{self.core} {self.config}]"


@dataclass
class ProjectResult:
    core: str
    config: str
    ok: bool
    errors: int
    warnings: int


@dataclass
class BuildLog:
    projects: list[ProjectResult] = field(default_factory=list)
    diagnostics: list[Diagnostic] = field(default_factory=list)

    @property
    def errors(self) -> list[Diagnostic]:
        return [d for d in self.diagnostics if d.severity == "error"]

    @property
    def warnings(self) -> list[Diagnostic]:
        return [d for d in self.diagnostics if d.severity == "warning"]


def repo_path(reported: str, core: str, config: str, tree_windows: str) -> str:
    """A path as gcc reports it -> repo-relative path.

    Sources of the project are relative to its build directory
    ('../SubGHz_Phy/App/x.c' from CM0PLUS/Debug_C2); linked sources are
    absolute in the Build Tree ('C:/Users/Simon/arcfw-bench/tree/Utilities/x.c').
    """
    p = reported.replace("\\", "/")
    tree = tree_windows.replace("\\", "/").rstrip("/") + "/"
    if p.lower().startswith(tree.lower()):
        return p[len(tree):]
    if re.match(r"^[A-Za-z]:/", p):
        return p  # outside the tree (toolchain headers): as reported
    return posixpath.normpath(posixpath.join(core, config, p))


def parse_log(text: str, tree_windows: str) -> BuildLog:
    log = BuildLog()
    core = config = ""
    seen: set[tuple] = set()
    for raw in text.splitlines():
        line = raw.rstrip("\r")
        m = _HEADER_RE.search(line)
        if m:
            config, core = m[1], CORE_OF_PROJECT.get(m[2], m[2])
            continue
        m = _SUMMARY_RE.search(line)
        if m:
            log.projects.append(ProjectResult(core, config, m[1] == "Finished", int(m[2]), int(m[3])))
            continue
        m = _DIAG_RE.match(line)
        if m:
            sev = "error" if m["sev"] != "warning" else "warning"
            d = Diagnostic(sev, m["msg"], repo_path(m["file"], core, config, tree_windows),
                           int(m["line"]), int(m["col"] or 0), core, config)
        else:
            m = _LINKER_RE.search(line)
            if not m:
                continue
            d = Diagnostic(m["sev"], m["msg"], core=core, config=config)
        key = (d.severity, d.message, d.file, d.line, d.core, d.config)
        if key not in seen:  # gcc repeats a header's diagnostic for every file including it
            seen.add(key)
            log.diagnostics.append(d)
    return log


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------


@dataclass
class BuildResult:
    build_id: str
    configs: list[str]
    ok: bool
    log: BuildLog
    elfs: dict[tuple[str, str], Path]      # (core, config) -> ELF (WSL path)
    problems: list[str] = field(default_factory=list)
    raw_log: str = ""


Runner = Callable[[list[str]], tuple[int, str]]


def run_process(cmd: list[str]) -> tuple[int, str]:
    # Windows executables need a Windows working directory.
    p = subprocess.run(cmd, capture_output=True, text=True, errors="replace", cwd="/mnt/c")
    return p.returncode, p.stdout + p.stderr


def _check_images(bid: str, elfs: dict[tuple[str, str], Path]) -> tuple[list[str], set[str]]:
    """Problems with the built images, and the configurations holding a stale image."""
    problems, stale = [], set()
    for (core, cfg), elf in sorted(elfs.items()):
        # The Build ID string in the image proves this build went into it.
        if not elf.exists():
            problems.append(f"{core} {cfg}: {elf} missing")
        elif bid.encode() not in elf.read_bytes():
            problems.append(f"{core} {cfg}: Build ID {bid} not found in {elf.name}")
            stale.add(cfg)
    return problems, stale


def _build_problems(code: int, log: BuildLog, configs: list[str]) -> list[str]:
    problems = []
    expected = {(core, cfg) for cfg in configs for core in CORES}
    reported = {(p.core, p.config) for p in log.projects}
    for core, cfg in sorted(expected - reported):
        problems.append(f"{core} {cfg}: no build summary in the CubeIDE output")
    for p in log.projects:
        if not p.ok:
            problems.append(f"{p.core} {p.config}: build failed, {p.errors} error(s)")
    if code != 0 and not problems:
        problems.append(f"stm32cubeidec exited with {code}")
    return problems


def build(repo: Path, bt: BuildTree, classes: list[str], overrides: dict[str, str],
          runner: Runner | None = None, cubeide: str | None = None,
          sync_tree: Callable[[Path, Path], None] | None = None,
          windows: Callable[[Path], str] = to_windows, clean: bool = False) -> BuildResult:
    """Sync, write the override header, build both cores of each class's configuration.

    An image without the Build ID after an error-free incremental build holds
    stale objects (make missed a dependency: objects compiled before the
    override header existed do not list it). That configuration is rebuilt
    clean once, then checked again.
    """
    runner = runner or run_process
    sync_tree = sync_tree or sync
    ide = cubeide or os.environ.get("BENCH_CUBEIDE", DEFAULT_CUBEIDE)
    configs = sorted({config_of(c) for c in classes})
    if not configs:
        raise ValueError("nothing to build: give at least one node class")
    bid = build_id(repo, overrides)
    sync_tree(repo, bt.tree)
    write_if_changed(bt.overrides_h, render_overrides_h(bid, overrides))
    elfs = {(core, cfg): bt.elf(core, cfg) for cfg in configs for core in CORES}

    code, out = runner(headless_command(ide, bt, configs, windows, clean=clean))
    log = parse_log(out, windows(bt.tree))
    problems = _build_problems(code, log, configs)
    if not problems:
        problems, stale = _check_images(bid, elfs)
        if stale and not clean:
            code, again = runner(headless_command(ide, bt, sorted(stale), windows, clean=True))
            out += "\n" + again
            relog = parse_log(again, windows(bt.tree))
            log.projects = [p for p in log.projects if p.config not in stale] + relog.projects
            log.diagnostics += relog.diagnostics
            problems = _build_problems(code, relog, sorted(stale)) or _check_images(bid, elfs)[0]
    return BuildResult(bid, configs, not problems, log, elfs, problems, out)
