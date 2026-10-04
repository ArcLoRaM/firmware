"""The programmer guard: nothing irreversible ever reaches a board."""

import os
import struct
import time
from pathlib import Path

import pytest

from bench.programmer import (Programmer, ProgrammerError, Refused, check_image, load_segments, validate)

DATA = Path(__file__).parent / "data"
SN = "003E002E3333511431363730"
CONNECT = ["-c", "port=SWD", f"sn={SN}", "mode=UR"]


def elf(segments):
    """A minimal 32-bit little-endian ELF with PT_LOAD segments [(paddr, filesz, memsz)]."""
    phoff, phentsize = 52, 32
    header = bytearray(52)
    header[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<I", header, 28, phoff)
    struct.pack_into("<HH", header, 42, phentsize, len(segments))
    ph = b"".join(struct.pack("<8I", 1, 0, paddr, paddr, filesz, memsz, 6, 4)
                  for paddr, filesz, memsz in segments)
    return bytes(header) + ph


def write_elf(tmp_path, name, segments):
    p = tmp_path / name
    p.write_bytes(elf(segments))
    return p


# --- grammar ----------------------------------------------------------------


@pytest.mark.parametrize("args", [
    ["-l", "st-link"],
    [*CONNECT, "-r32", "0x1FFF7590", "12"],
    [*CONNECT, "-w", r"C:\b\cm0.elf", "-v", "-w", r"C:\b\cm4.elf", "-v", "-rst"],
    [*CONNECT, "-rst"],
    ["-c", "port=SWD", f"sn={SN}", "mode=HOTPLUG", "-hardRst"],
])
def test_allowed_operations(args):
    validate(args)


@pytest.mark.parametrize("args, why", [
    # Irreversible or bricking: option bytes (RDP 2 is permanent), OTP, security, erase.
    ([*CONNECT, "-ob", "RDP=0xCC"], "not allowed"),
    ([*CONNECT, "-ob", "displ"], "not allowed"),
    ([*CONNECT, "-otp", "program"], "not allowed"),
    ([*CONNECT, "-e", "all"], "not allowed"),
    ([*CONNECT, "-e", "0"], "not allowed"),
    ([*CONNECT, "-rdu"], "not allowed"),
    ([*CONNECT, "-w32", "0x1FFF7000", "0x12345678"], "not allowed"),
    ([*CONNECT, "-w8", "0x1FFF7800", "0xAA"], "not allowed"),
    ([*CONNECT, "-fillmemory", "0x08000000", "size=4"], "not allowed"),
    ([*CONNECT, "-w", r"C:\b\image.bin", "0x1FFF7000"], "only ELF"),
    ([*CONNECT, "-w", r"C:\b\image.hex"], "only ELF"),
    ([*CONNECT, "-r32", "0x08000000", "4"], "only the chip UID"),
    ([*CONNECT, "-startfus"], "not allowed"),
    # Connection must be explicit and name the probe.
    (["-c", "port=SWD", "mode=UR", "-rst"], "must start with"),
    (["-c", "port=SWD", "sn=003E", "mode=UR", "-rst"], "must start with"),
    (["-c", "port=USB1", f"sn={SN}", "mode=UR", "-rst"], "must start with"),
    (["-rst"], "must start with"),
    (CONNECT, "no operation"),
    (["-l"], "must start with"),
])
def test_refused_operations(args, why):
    with pytest.raises(Refused, match=why):
        validate(args)


def test_a_refused_operation_never_reaches_the_programmer():
    calls = []
    p = Programmer(exe="prog.exe", runner=lambda cmd: calls.append(cmd) or (0, ""))
    with pytest.raises(Refused):
        p.run([*CONNECT, "-ob", "RDP=0xCC"])
    assert calls == []


# --- images -----------------------------------------------------------------


def test_segments_without_bytes_are_not_written():
    data = elf([(0x08000000, 0x96B8, 0x96B8), (0x20000020, 0, 0x1DA0)])
    assert load_segments(data) == [(0x08000000, 0x96B8)]


def test_images_must_stay_in_their_cores_flash(tmp_path):
    cm4 = write_elf(tmp_path, "cm4.elf", [(0x08000000, 0x96B8, 0x96B8), (0x080096B8, 0x20, 0x20)])
    cm0 = write_elf(tmp_path, "cm0.elf", [(0x08020000, 0xF0A8, 0xF0A8), (0x0802F120, 0x48, 0x348)])
    check_image(cm4, "CM4")
    check_image(cm0, "CM0PLUS")
    with pytest.raises(Refused, match="outside the CM0PLUS flash"):
        check_image(cm4, "CM0PLUS")  # swapped images
    with pytest.raises(Refused, match="outside the CM4 flash"):
        check_image(cm0, "CM4")


@pytest.mark.parametrize("segment, what", [
    ((0x1FFF7000, 8, 8), "OTP"),
    ((0x1FFF7800, 16, 16), "option bytes"),
    ((0x0801FFF0, 0x20, 0x20), "across the core boundary"),
    ((0x20000000, 4, 4), "RAM"),
])
def test_images_writing_elsewhere_are_refused(tmp_path, segment, what):
    p = write_elf(tmp_path, "bad.elf", [(0x08000000, 0x100, 0x100), segment])
    with pytest.raises(Refused, match="outside the CM4 flash"):
        check_image(p, "CM4")


def test_not_an_elf_is_refused(tmp_path):
    p = tmp_path / "x.elf"
    p.write_bytes(b"not an elf")
    with pytest.raises(Refused, match="not a 32-bit"):
        check_image(p, "CM4")


REAL = Path("/mnt/c/Users/Simon/arcfw-bench/tree")


def _settled(path: Path) -> bool:
    """A finished image: it exists, is not empty, and was not written in the last 10 s. The Build Tree
    is shared by every session, so another one may be linking this very file."""
    return path.exists() and path.stat().st_size > 0 and time.time() - path.stat().st_mtime > 10


def test_an_image_being_written_is_not_settled(tmp_path):
    empty = tmp_path / "a.elf"
    empty.write_bytes(b"")
    fresh = tmp_path / "b.elf"
    fresh.write_bytes(b"\x7fELF")
    old = tmp_path / "c.elf"
    old.write_bytes(b"\x7fELF")
    os.utime(old, (time.time() - 60, time.time() - 60))
    assert [_settled(empty), _settled(fresh), _settled(old), _settled(tmp_path / "none.elf")] == [False, False, True, False]


def test_the_real_images_pass():
    images = {"CM4": REAL / "CM4/Debug_C2/ArcLoRaM_Base_CM4.elf", "CM0PLUS": REAL / "CM0PLUS/Debug_C2/ArcLoRaM_Base_CM0PLUS.elf"}
    if not all(_settled(p) for p in images.values()):
        pytest.skip("no finished bench build (missing, empty, or being linked by another session)")
    for core, path in images.items():
        check_image(path, core)


# --- programmer ---------------------------------------------------------------


class Fake:
    def __init__(self, out, code=0):
        self.out, self.code, self.cmds = out, code, []

    def __call__(self, cmd):
        self.cmds.append(cmd)
        return self.code, self.out


def test_probe_list_from_the_real_output():
    p = Programmer(exe="prog.exe", runner=Fake((DATA / "programmer-list.txt").read_text()))
    assert [(x.sn, x.board) for x in p.probes()] == [
        ("003D003D3234510833353533", "NUCLEO-WL55JC"), ("003E002E3333511431363730", "NUCLEO-WL55JC")]


def test_uid_from_the_real_output():
    fake = Fake((DATA / "programmer-uid.txt").read_text())
    assert Programmer(exe="prog.exe", runner=fake).read_uid(SN) == (0x0026001A, 0x32325014, 0x20383543)
    assert fake.cmds == [["prog.exe", *CONNECT, "-r32", "0x1FFF7590", "12"]]


def test_programmer_errors_are_raised():
    p = Programmer(exe="prog.exe", runner=Fake((DATA / "programmer-hotplug-stop2.txt").read_text()))
    with pytest.raises(ProgrammerError, match="No STM32 target found"):
        p.read_uid(SN)


def test_flash_writes_both_cores_in_one_session(tmp_path):
    cm4 = write_elf(tmp_path, "cm4.elf", [(0x08000000, 0x100, 0x100)])
    cm0 = write_elf(tmp_path, "cm0.elf", [(0x08020000, 0x100, 0x100)])
    fake = Fake("File download complete\n")
    Programmer(exe="prog.exe", runner=fake, windows=str).flash(SN, {"CM4": cm4, "CM0PLUS": cm0})
    assert fake.cmds == [["prog.exe", *CONNECT, "-w", str(cm0), "-v", "-w", str(cm4), "-v", "-rst"]]


def test_flash_refuses_one_core_alone_or_a_bad_image(tmp_path):
    cm4 = write_elf(tmp_path, "cm4.elf", [(0x08000000, 0x100, 0x100)])
    otp = write_elf(tmp_path, "otp.elf", [(0x1FFF7000, 8, 8)])
    fake = Fake("")
    p = Programmer(exe="prog.exe", runner=fake, windows=str)
    with pytest.raises(Refused, match="both cores"):
        p.flash(SN, {"CM4": cm4})
    with pytest.raises(Refused, match="outside the CM0PLUS flash"):
        p.flash(SN, {"CM4": cm4, "CM0PLUS": otp})
    assert fake.cmds == []
