"""The only way bench talks to a board over SWD: STM32_Programmer_CLI behind an allowlist.

bench must never do anything to a board that cannot be undone by flashing
it again. Irreversible or bricking operations exist in the programmer and
are refused here, whatever the caller asks:

- option bytes (-ob): readout protection (RDP level 2 is permanent),
  write protection, boot configuration, security (ESE);
- OTP (one-time programmable area, 0x1FFF7000-0x1FFF73FF): bits written once
  are written forever;
- mass erase, direct memory writes (-w8/-w16/-w32/-w64, -fillmemory),
  binary files written at a caller-given address;
- anything this module does not know.

Every argument list is checked token by token against a small grammar
before the programmer runs (validate), and every image is checked to write
only inside its core's half of main flash (check_image) before it is
written. A refused operation raises Refused; nothing is sent to the board.
"""

from __future__ import annotations

import os
import re
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from bench.winpath import to_windows

DEFAULT_PROGRAMMER = ("/mnt/c/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/"
                      "com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.500.202603051304/"
                      "tools/bin/STM32_Programmer_CLI.exe")

UID_ADDR = 0x1FFF7590
UID_SIZE = 12

#: Main flash of the STM32WL55JC, split between the cores by the linker scripts.
FLASH_REGION = {"CM4": (0x08000000, 0x08020000), "CM0PLUS": (0x08020000, 0x08040000)}

_SN_RE = re.compile(r"^sn=[0-9A-F]{24}$")


class Refused(Exception):
    """An operation bench never performs on a board."""


# ---------------------------------------------------------------------------
# Argument grammar
# ---------------------------------------------------------------------------


def validate(args: list[str]) -> None:
    """Refuse any argument list outside the grammar below.

        -l st-link
        -c port=SWD sn=<24 hex> mode=UR|HOTPLUG  followed by any of:
            -r32 0x1FFF7590 12        (chip UID, read only)
            -w <path>.elf [-v]        (image, checked by check_image)
            -rst | -hardRst
    """
    if args == ["-l", "st-link"]:
        return
    if len(args) < 4 or args[0] != "-c" or args[1] != "port=SWD" or not _SN_RE.match(args[2]) \
            or args[3] not in ("mode=UR", "mode=HOTPLUG"):
        raise Refused(f"programmer arguments must start with -c port=SWD sn=<serial> mode=UR|HOTPLUG: {args}")
    i = 4
    if i == len(args):
        raise Refused("a connection with no operation")
    while i < len(args):
        a = args[i]
        if a == "-r32":
            if args[i + 1:i + 3] != [f"0x{UID_ADDR:08X}", str(UID_SIZE)]:
                raise Refused(f"only the chip UID may be read: {args[i:i + 3]}")
            i += 3
        elif a == "-w":
            if i + 1 >= len(args) or not args[i + 1].lower().endswith(".elf"):
                raise Refused("only ELF images may be written (a binary would need a caller-given address)")
            i += 2
            if i < len(args) and args[i] == "-v":
                i += 1
        elif a in ("-rst", "-hardRst"):
            i += 1
        else:
            raise Refused(f"programmer operation {a!r} is not allowed (bench never touches option bytes, "
                          "OTP, protection or security, and never mass erases)")


# ---------------------------------------------------------------------------
# Images
# ---------------------------------------------------------------------------


def load_segments(elf: bytes) -> list[tuple[int, int]]:
    """(load address, size) of every ELF segment with bytes to write."""
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise Refused("not a 32-bit little-endian ELF image")
    phoff, = struct.unpack_from("<I", elf, 28)
    phentsize, phnum = struct.unpack_from("<HH", elf, 42)
    segments = []
    for i in range(phnum):
        ptype, _off, _vaddr, paddr, filesz = struct.unpack_from("<5I", elf, phoff + i * phentsize)
        if ptype == 1 and filesz > 0:  # PT_LOAD with content
            segments.append((paddr, filesz))
    return segments


def check_image(path: Path, core: str) -> None:
    """Refuse an image that would write anywhere but its core's half of main flash."""
    lo, hi = FLASH_REGION[core]
    segments = load_segments(path.read_bytes())
    if not segments:
        raise Refused(f"{path.name}: nothing to write")
    for addr, size in segments:
        if not (lo <= addr and addr + size <= hi):
            raise Refused(f"{path.name}: segment 0x{addr:08X}+0x{size:X} is outside the {core} flash "
                          f"0x{lo:08X}-0x{hi - 1:08X}")


# ---------------------------------------------------------------------------
# Programmer
# ---------------------------------------------------------------------------

Runner = Callable[[list[str]], tuple[int, str]]


def _run(cmd: list[str]) -> tuple[int, str]:
    p = subprocess.run(cmd, capture_output=True, text=True, errors="replace", cwd="/mnt/c")
    return p.returncode, (p.stdout + p.stderr).replace("\r", "")


@dataclass
class Probe:
    sn: str
    board: str


class ProgrammerError(Exception):
    pass


_PROBE_RE = re.compile(r"ST-LINK SN\s*:\s*([0-9A-F]{24}).*?Board Name\s*:\s*(\S+)", re.S)
_UID_RE = re.compile(r"0x1FFF7590\s*:\s*([0-9A-F]{8})\s+([0-9A-F]{8})\s+([0-9A-F]{8})")


class Programmer:
    def __init__(self, exe: str | None = None, runner: Runner | None = None,
                 windows: Callable[[Path], str] = to_windows) -> None:
        self.exe = exe or os.environ.get("BENCH_PROGRAMMER", DEFAULT_PROGRAMMER)
        self.runner = runner or _run
        self.windows = windows

    def run(self, args: list[str]) -> str:
        validate(args)
        code, out = self.runner([self.exe, *args])
        if code != 0 or "Error:" in out:
            errors = [ln.strip() for ln in out.splitlines() if "Error" in ln] or [f"exit code {code}"]
            raise ProgrammerError("; ".join(dict.fromkeys(errors)))
        return out

    @staticmethod
    def _connect(sn: str, mode: str = "UR") -> list[str]:
        return ["-c", "port=SWD", f"sn={sn}", f"mode={mode}"]

    def probes(self) -> list[Probe]:
        out = self.run(["-l", "st-link"])
        blocks = out.split("ST-Link Probe ")[1:]
        return [Probe(m[1], m[2]) for m in (_PROBE_RE.search(b) for b in blocks) if m]

    def read_uid(self, sn: str) -> tuple[int, int, int]:
        """Chip UID over SWD. Connects under reset: the board reboots (the firmware sleeps in
        STOP2, where the debug port is off, so a hot-plug connection fails)."""
        out = self.run([*self._connect(sn), "-r32", f"0x{UID_ADDR:08X}", str(UID_SIZE)])
        m = _UID_RE.search(out)
        if not m:
            raise ProgrammerError(f"no UID in the programmer output for probe {sn}")
        return int(m[1], 16), int(m[2], 16), int(m[3], 16)

    def flash(self, sn: str, images: dict[str, Path]) -> str:
        """Write and verify both cores' images in one session, then reset."""
        if set(images) != set(FLASH_REGION):
            raise Refused(f"both cores are flashed together, from one build: got {sorted(images)}")
        args = self._connect(sn)
        for core in ("CM0PLUS", "CM4"):  # the CM4 starts the CM0+: write it last
            check_image(images[core], core)
            args += ["-w", self.windows(images[core]), "-v"]
        return self.run([*args, "-rst"])

    def reset(self, sn: str) -> str:
        return self.run([*self._connect(sn), "-rst"])
