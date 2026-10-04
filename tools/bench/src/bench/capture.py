"""The always-on Capture: one multi-port `arclog capture` on Windows, owned by bench.

It is the only process that opens the boards' COM ports; runs read its
files. It runs natively on Windows (Windows owns the COM ports), detached, so
it outlives the session that started it.
"""

from __future__ import annotations

import json
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from bench.boards import capture_node

CAPTURE_DIR = Path("tools/arclog/runs/bench")
ARCLOG_DIR = Path("tools/arclog")

PowerShell = Callable[[str], str]


def powershell(script: str) -> str:
    return subprocess.run(["powershell.exe", "-NoProfile", "-Command", script],
                          capture_output=True, text=True, check=True, cwd="/mnt/c").stdout.replace("\r", "")


def wsl_to_windows(path: Path) -> str:
    """Any WSL path as Windows sees it (\\\\wsl.localhost\\<distro>\\... or C:\\...)."""
    return subprocess.run(["wslpath", "-w", str(path)], capture_output=True, text=True, check=True).stdout.strip()


@dataclass
class CaptureProcess:
    pid: int
    ports: list[str]
    out: str               # output directory, as given on its command line

    def is_bench(self, bench_out: str) -> bool:
        return self.out.rstrip("\\/").lower() == bench_out.rstrip("\\/").lower()


_LIST_PS = ("Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'arclog.exe' -and "
            "$_.CommandLine -match ' capture ' } | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress")


def parse_processes(json_text: str) -> list[CaptureProcess]:
    if not json_text.strip():
        return []
    data = json.loads(json_text)
    procs = []
    for p in data if isinstance(data, list) else [data]:
        cmd = p.get("CommandLine") or ""
        ports = re.findall(r"--port\s+(\S+)", cmd)
        out = re.search(r"--out\s+(\"[^\"]+\"|\S+)", cmd)
        procs.append(CaptureProcess(int(p["ProcessId"]), ports, out[1].strip('"') if out else ""))
    return procs


def capture_command(uv_project: str, out_dir: str, ports: list[str]) -> str:
    """Windows command line of the capture; stderr goes next to the files for `status`."""
    args = [f'uv.exe run --no-project --with "{uv_project}" arclog capture']
    # COM ports by number, then the Pi Nodes' TCP sources by name.
    for port in sorted(ports, key=lambda p: (0, int(p[3:]), "") if p.upper().startswith("COM") else (1, 0, p)):
        args.append(f"--port {port} --node {capture_node(port)}")
    args.append(f'--out "{out_dir}" --quiet')
    return f'cmd.exe /c {" ".join(args)} 2>> "{out_dir}\\capture.err"'


@dataclass
class Status:
    running: CaptureProcess | None
    foreign: list[CaptureProcess]
    missing: list[str]         # present ports the bench capture does not record


class Capture:
    def __init__(self, repo: Path, ps: PowerShell = powershell,
                 windows: Callable[[Path], str] = wsl_to_windows, data: Path | None = None) -> None:
        """`repo` provides arclog; the capture files go under `data` (the main checkout, shared by all
        worktrees) and under `repo` when it is not given."""
        self.repo = repo
        self.ps = ps
        self.dir = (data or repo) / CAPTURE_DIR
        self.out_windows = windows(self.dir)
        self.arclog_windows = windows(repo / ARCLOG_DIR)
        self.left_alone: list[str] = []

    def status(self, ports: list[str]) -> Status:
        procs = parse_processes(self.ps(_LIST_PS))
        bench = [p for p in procs if p.is_bench(self.out_windows)]
        running = bench[0] if bench else None
        covered = set(running.ports) if running else set()
        return Status(running, [p for p in procs if not p.is_bench(self.out_windows)],
                      sorted(set(ports) - covered))

    def _stop(self, proc: CaptureProcess) -> None:
        """Stop the whole tree (cmd.exe -> uv.exe -> arclog.exe -> python.exe): Windows does not
        stop children with their parent, and the python.exe child is what holds the ports."""
        self.ps(f"$id = {proc.pid}; "
                "while ($true) { $parent = (Get-CimInstance Win32_Process -Filter \"ProcessId=$id\").ParentProcessId; "
                "$pp = Get-CimInstance Win32_Process -Filter \"ProcessId=$parent\"; "
                "if ($pp -and $pp.Name -in 'cmd.exe','uv.exe') { $id = $parent } else { break } }; "
                "taskkill.exe /T /F /PID $id | Out-Null")

    def up(self, ports: list[str], replace: bool = False,
           required: list[str] | None = None) -> tuple[Status, str]:
        """Make sure the bench capture records every given port. Returns the status before, and what was done.

        `required` are the ports the caller works with. A foreign capture that holds only other ports
        (another worktree's run on its own boards) is left alone and those ports are not recorded here
        (`left_alone`); one that holds a required port is an error unless `replace`.
        """
        self.left_alone = []
        st = self.status(ports)
        holding = [p for p in st.foreign if set(p.ports) & set(ports)]
        if holding and not replace and required is not None \
                and not any(set(p.ports) & set(required) for p in holding):
            self.left_alone = [x for x in ports if any(x in p.ports for p in holding)]
            ports = [x for x in ports if x not in self.left_alone]
            if not ports:
                return st, "skipped"
            covered = set(st.running.ports) if st.running else set()
            st = Status(st.running, st.foreign, sorted(set(ports) - covered))
            holding = []
        if holding and not replace:
            raise RuntimeError("another capture holds the ports: "
                               + "; ".join(f"pid {p.pid} {','.join(p.ports)} -> {p.out}" for p in holding)
                               + " (stop it, or use --replace)")
        if st.running and not st.missing and not holding:
            return st, "running"
        for p in holding:
            self._stop(p)
        if st.running:
            self._stop(st.running)
        self.dir.mkdir(parents=True, exist_ok=True)
        cmdline = capture_command(self.arclog_windows, self.out_windows, sorted(set(ports) | set(
            st.running.ports if st.running else [])))
        escaped = cmdline.replace("'", "''")
        self.ps(f"Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments "
                f"@{{CommandLine='{escaped}'; CurrentDirectory='C:\\'}} | Out-Null")
        return st, "restarted" if st.running or holding else "started"
