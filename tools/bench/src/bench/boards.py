"""Bench boards: probe serial number, trace COM port, chip UID and Node ID.

The probe serial number is how bench talks to a board; the chip UID is how
it recognises it. The Node ID comes from the firmware's own UID table
(Common/Protocol/node_id.c), the only list of boards: there is no host-side
copy to keep in step.
"""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import urlsplit

NODE_ID_C = Path("Common/Protocol/node_id.c")

Uid = tuple[int, int, int]

_ENTRY_RE = re.compile(r"^\s*\{ \{ 0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u \}, (\d+)u \},"
                       r"(?:\s*/\*.*\*/)?\s*$")
_END_RE = re.compile(r"^\s*\{ \{ 0u, 0u, 0u \}, NODE_ID_UNPROVISIONED \},\s*$")


def parse_node_table(text: str) -> dict[Uid, int]:
    """UID -> Node ID from node_id.c. Every line of k_table must match the strict entry format."""
    lines = text.splitlines()
    try:
        start = next(i for i, ln in enumerate(lines) if "k_table[] = {" in ln)
    except StopIteration:
        raise ValueError("node_id.c: no k_table") from None
    table: dict[Uid, int] = {}
    for ln in lines[start + 1:]:
        stripped = ln.strip()
        if _END_RE.match(ln):
            return table
        if not stripped or stripped.startswith("/*"):
            continue
        m = _ENTRY_RE.match(ln)
        if not m:
            raise ValueError(f"node_id.c: entry not in the format "
                             f"'{{ {{ 0x0014008Fu, 0x32325014u, 0x20383543u }}, 1u }},': {stripped}")
        uid = (int(m[1], 16), int(m[2], 16), int(m[3], 16))
        if uid in table:
            raise ValueError(f"node_id.c: UID {format_uid(uid)} registered twice")
        if int(m[4]) in table.values():
            raise ValueError(f"node_id.c: Node ID {m[4]} registered twice")
        table[uid] = int(m[4])
    raise ValueError("node_id.c: k_table has no end marker")


def load_node_table(repo: Path) -> dict[Uid, int]:
    return parse_node_table((repo / NODE_ID_C).read_text(encoding="utf-8"))


def format_uid(uid: Uid) -> str:
    """As the firmware logs it in BOOT: 24 lowercase hex digits."""
    return "".join(f"{w:08x}" for w in uid)


def parse_uid(text: str) -> Uid:
    if not re.fullmatch(r"[0-9a-fA-F]{24}", text):
        raise ValueError(f"not a UID: {text!r}")
    return int(text[0:8], 16), int(text[8:16], 16), int(text[16:24], 16)


# ---------------------------------------------------------------------------
# COM ports
# ---------------------------------------------------------------------------

# With no port present, Get-PnpDevice fails ("No matching Win32_PnPEntity"), which means no port; PowerShell
# still exits 1 after an ignored error, hence the explicit exit 0.
_PORTS_PS = ("Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue | ForEach-Object { "
             "$p = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_Parent).Data; "
             "'{0}|{1}' -f $_.FriendlyName, $p }; exit 0")
_PORT_RE = re.compile(r"STLink Virtual COM Port \((COM\d+)\)\|USB\\VID_0483&PID_[0-9A-F]{4}\\([0-9A-F]{24})")


def parse_ports(text: str) -> dict[str, str]:
    """Probe serial number -> COM port, from the Windows device tree (the VCP's USB parent is the probe)."""
    return {m[2]: m[1] for m in _PORT_RE.finditer(text)}


def query_ports() -> dict[str, str]:
    # Windows PowerShell writes its OEM codepage, not UTF-8: a port with an accented friendly name
    # (a Bluetooth COM port on a localised Windows) would break strict decoding. The port regex only
    # needs ASCII, so undecodable bytes are replaced.
    out = subprocess.run(["powershell.exe", "-NoProfile", "-Command", _PORTS_PS],
                         capture_output=True, check=True, cwd="/mnt/c").stdout
    return parse_ports(out.decode("utf-8", errors="replace"))


def capture_node(port: str) -> str:
    """Capture file prefix of a port: 'COM9' -> 'com9'.

    A Pi Node's log is read over TCP and named by the label in the URL's path
    ('tcp://100.64.0.11:4000/nuna-node-01' -> 'nuna-node-01'), not by its host, which may be an address.
    """
    if port.startswith("tcp://"):
        url = urlsplit(port)
        return (url.path.strip("/") or url.hostname or port).lower()
    return port.lower()


@dataclass
class Board:
    sn: str
    port: str | None = None
    uid: Uid | None = None
    uid_source: str = ""        # "trace" (last BOOT in the capture) or "swd"
    node_id: int | None = None  # None: UID unknown or not in node_id.c
    build: str | None = None    # Build ID of the last BOOT in the capture
    remote: bool = False        # a Pi Node: `sn` is its configured name, `port` its log URL

    @property
    def node(self) -> str | None:
        return capture_node(self.port) if self.port else None
