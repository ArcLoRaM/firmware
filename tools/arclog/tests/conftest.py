"""Synthetic C3 -> C2 sync runs for report and merge tests."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest

from arclog.model import format_host_time

T0 = datetime(2026, 9, 25, 12, 0, 0, tzinfo=timezone.utc)
PER_CELL_MS = 3000
FRAME_S = 60


def dev_ts(t: datetime) -> str:
    return t.strftime("%y%m%dT%H%M%S.") + f"{t.microsecond // 100:04d}"


@dataclass
class NodeLog:
    """Builds the capture file of one node, line by line."""

    node: str
    lines: list[str] = field(default_factory=list)
    seq: dict[str, int] = field(default_factory=lambda: {"0": 0, "4": 0})

    def emit(self, t: datetime, core: str, mod: str, lvl: str, event: str, **fields) -> None:
        body = " ".join(f"{k}={v}" for k, v in fields.items())
        s = self.seq[core]
        self.seq[core] = (s + 1) % 256
        dev = f"{dev_ts(t)} {core}{mod} {lvl} #{s:02x} {event}" + (f" {body}" if body else "")
        self.lines.append(f"{format_host_time(t)}\t{dev}")

    def raw(self, t: datetime, text: str) -> None:
        self.lines.append(f"{format_host_time(t)}\t{text}")

    def write(self, directory: Path) -> Path:
        path = directory / f"{self.node}-{T0.strftime('%Y%m%d')}.log"
        path.write_text("\n".join(self.lines) + "\n", encoding="utf-8")
        return path


def ms_of_day(t: datetime) -> int:
    return ((t.hour * 60 + t.minute) * 60 + t.second) * 1000 + t.microsecond // 1000


C3_UID = "002000415642500a20383353"
C2_UID = "002000415642500a20383354"


def build_run(drift_ppm: float = 20.0, frames: int = 120, hdr_minus_pre: int = 530,
              tx_start_lag: int = 5) -> tuple[NodeLog, NodeLog]:
    """C3 sends 3 Sync cells per frame; C2 locks on frame 0 (cells 0-2), then
    receives cell 0 of every frame while WARM. The C2 clock drifts by
    drift_ppm; |err| >= 8 ms triggers a tier-2 RTC_SHIFT that zeroes it."""
    c3, c2 = NodeLog("c3"), NodeLog("c2")
    c3.emit(T0, "0", "S", "A", "BOOT", cls="C3", id="1", uid=C3_UID, fw="1.5.0")
    c2.emit(T0, "0", "S", "A", "BOOT", cls="C2", id="2", uid=C2_UID, fw="1.5.0")
    c2.emit(T0, "0", "M", "L", "MAC_INIT", cls="C2", st="SCAN", clk="COLD")

    offset_ms = 0.0          # C2 clock minus C3 clock
    last_t = None
    clk = "COLD"
    good = 0
    for f in range(frames):
        t_phase = T0 + timedelta(seconds=10 + f * FRAME_S)
        ep = ms_of_day(t_phase)
        c3.emit(t_phase, "0", "Y", "M", "SYNC_EPOCH", ph=0, ep=ep)
        cells = range(3) if f == 0 else range(1)
        for ce in cells:
            t_cell = t_phase + timedelta(milliseconds=ce * PER_CELL_MS)
            plan = ep + ce * PER_CELL_MS
            c3.emit(t_cell, "0", "Y", "M", "SYNC_TX", ph=0, ce=ce, ep=ep, plan=plan, send=plan + 2,
                    freq=868100000)
            t_done = t_cell + timedelta(milliseconds=tx_start_lag + 991)
            c3.emit(t_done, "0", "R", "M", "TX_DONE", sz=10, toa=991,
                    end=plan + tx_start_lag + 991, start=plan + tx_start_lag)

            # Receiver
            if last_t is not None and clk == "WARM":
                offset_ms += drift_ppm * 1e-6 * (t_cell - last_t).total_seconds() * 1000.0
            last_t = t_cell
            st = plan + round(offset_ms)
            exp = plan
            c2.emit(t_done, "0", "R", "M", "RX_DONE", sz=10, rssi=-40, snr=9, pre=st,
                    hdr=st + hdr_minus_pre, rxd=st + 991, toa=991, st=st)
            err = st - exp
            if clk == "COLD":
                c2.emit(t_done, "0", "Y", "M", "SYNC_RX", ph=0, ce=ce, ep=ep, st=st, exp=exp, err=err, erru=err * 1000,
                        clk="COLD", act="set")
                c2.emit(t_done, "0", "Y", "L", "RTC_SET", old=st + 991, new=exp + 991, d=-err,
                        date="260925", shift="ok")
                c2.emit(t_done, "0", "Y", "L", "CLK", **{"from": "COLD", "to": "ACQ", "why": "rtc_set"})
                clk, offset_ms = "ACQ", 0.0
            elif clk == "ACQ":
                c2.emit(t_done, "0", "Y", "M", "SYNC_RX", ph=0, ce=ce, ep=ep, st=st, exp=exp, err=err, erru=err * 1000,
                        clk="ACQ", act="good")
                good += 1
                if good == 2:
                    c2.emit(t_done, "0", "Y", "L", "CLK", **{"from": "ACQ", "to": "WARM", "why": "lock"})
                    c2.emit(t_done, "0", "M", "L", "MAC_ST", **{"from": "SCAN", "to": "SYNC", "why": "lock"})
                    clk = "WARM"
            else:
                act = "t1" if abs(err) < 8 else "t2"
                c2.emit(t_done, "0", "Y", "M", "SYNC_RX", ph=0, ce=ce, ep=ep, st=st, exp=exp, err=err, erru=err * 1000,
                        clk="WARM", act=act)
                if act == "t2":
                    c2.emit(t_done, "0", "Y", "M", "RTC_SHIFT", err=err, ticks=abs(err) * 4, res="ok")
                    offset_ms -= err
    return c3, c2


@pytest.fixture
def run_files(tmp_path: Path) -> tuple[Path, Path]:
    c3, c2 = build_run()
    return c3.write(tmp_path), c2.write(tmp_path)


@pytest.fixture
def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]
