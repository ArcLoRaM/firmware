"""Boards, the Node ID table, COM ports, capture supervision and the flash flow."""

import json
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest

from arclog.capture import DailyWriter
from bench.boards import Board, format_uid, load_node_table, parse_node_table, parse_ports, parse_uid
from bench.build import BuildLog, BuildResult
from bench.capture import Capture, capture_command, parse_processes
from bench.flash import boot_check, discover, flash_nodes, last_boot, select
from bench.programmer import Probe

REPO = Path(__file__).resolve().parents[3]
UID1 = (0x0014008F, 0x32325014, 0x20383543)
UID2 = (0x0026001A, 0x32325014, 0x20383543)
SN8, SN9 = "003D003D3234510833353533", "003E002E3333511431363730"

TABLE = """
static const NodeIdEntry_t k_table[] = {
    /* { { w0, w1, w2 }, node_id }, */
    { { 0x0014008Fu, 0x32325014u, 0x20383543u }, 1u },   /* bench C3 */
    { { 0x0026001Au, 0x32325014u, 0x20383543u }, 2u },

    /* End marker: keeps the array non-empty, not counted. */
    { { 0u, 0u, 0u }, NODE_ID_UNPROVISIONED },
};
"""


# --- Node ID table ------------------------------------------------------------


def test_node_table():
    assert parse_node_table(TABLE) == {UID1: 1, UID2: 2}


def test_the_real_node_table_is_in_the_strict_format():
    table = load_node_table(REPO)
    assert table[UID1] == 1 and table[UID2] == 2


@pytest.mark.parametrize("entry, problem", [
    ("{ { 0x14008Fu, 0x32325014u, 0x20383543u }, 3u },", "not in the format"),
    ("{ { 0x0014008fu, 0x32325014u, 0x20383543u }, 3u },", "not in the format"),  # lowercase hex
    ("{ {0x00AA008Fu, 0x32325014u, 0x20383543u}, 3u },", "not in the format"),
    ("{ { 0x0014008Fu, 0x32325014u, 0x20383543u }, 3u },", "UID 0014008f3232501420383543 registered twice"),
    ("{ { 0x00AA008Fu, 0x32325014u, 0x20383543u }, 2u },", "Node ID 2 registered twice"),
])
def test_table_entries_must_be_strict_and_unique(entry, problem):
    text = TABLE.replace("    /* End marker", f"    {entry}\n    /* End marker")
    with pytest.raises(ValueError, match=problem):
        parse_node_table(text)


def test_uid_text():
    assert format_uid(UID2) == "0026001a3232501420383543"
    assert parse_uid("0026001a3232501420383543") == UID2


# --- COM ports ------------------------------------------------------------------

PORTS = (
    "STMicroelectronics STLink Virtual COM Port (COM9)|USB\\VID_0483&PID_374E\\003E002E3333511431363730\n"
    "STMicroelectronics STLink Virtual COM Port (COM8)|USB\\VID_0483&PID_374E\\003D003D3234510833353533\n"
    "Communications Port (COM1)|ACPI\\PNP0501\\0\n"
)


def test_ports_come_from_the_probe_that_owns_them():
    assert parse_ports(PORTS) == {SN9: "COM9", SN8: "COM8"}


# --- capture process ------------------------------------------------------------

BENCH_OUT = r"\\wsl.localhost\Ubuntu\home\simon\Projects\Firmware\tools\arclog\runs\bench"


def proc_json(*procs):
    return json.dumps([{"ProcessId": pid, "CommandLine": cmd} for pid, cmd in procs])


def test_capture_processes_are_parsed():
    cmd = (r'"C:\u\arclog.exe" capture --port COM6 --node com6 --port COM9 --node com9 '
           r'--out \\wsl.localhost\Ubuntu\x\runs\2026-09-29-nodeid --quiet')
    [p] = parse_processes(proc_json((10960, cmd)))
    assert (p.pid, p.ports, p.out) == (10960, ["COM6", "COM9"], r"\\wsl.localhost\Ubuntu\x\runs\2026-09-29-nodeid")
    assert parse_processes("") == []


def test_capture_command_line():
    cmd = capture_command(r"\\wsl.localhost\U\arclog", BENCH_OUT, ["COM9", "COM10", "COM8"])
    assert "--port COM8 --node com8 --port COM9 --node com9 --port COM10 --node com10" in cmd
    assert cmd.startswith("cmd.exe /c uv.exe run --no-project")
    assert cmd.endswith(f'2>> "{BENCH_OUT}\\capture.err"')


class FakePs:
    def __init__(self, procs):
        self.procs, self.scripts = procs, []

    def __call__(self, script):
        self.scripts.append(script)
        return proc_json(*self.procs) if script.startswith("Get-CimInstance") else ""


def cap(tmp_path, ps):
    return Capture(tmp_path, ps=ps, windows=lambda p: BENCH_OUT if p.name == "bench" else r"\\x\arclog")


def bench_proc(pid, ports):
    return pid, f"arclog.exe capture {' '.join(f'--port {p}' for p in ports)} --out {BENCH_OUT} --quiet"


def test_capture_up_leaves_a_complete_bench_capture_alone(tmp_path):
    ps = FakePs([bench_proc(5, ["COM8", "COM9"])])
    assert cap(tmp_path, ps).up(["COM8", "COM9"])[1] == "running"
    assert len(ps.scripts) == 1


def test_capture_up_starts_one_when_none_runs(tmp_path):
    ps = FakePs([])
    assert cap(tmp_path, ps).up(["COM9", "COM8"])[1] == "started"
    create = ps.scripts[-1]
    assert "Win32_Process -MethodName Create" in create
    assert "--port COM8 --node com8 --port COM9 --node com9" in create


def test_capture_up_restarts_its_own_capture_for_a_new_port(tmp_path):
    ps = FakePs([bench_proc(5, ["COM9"])])
    assert cap(tmp_path, ps).up(["COM8", "COM9"])[1] == "restarted"
    assert any("taskkill.exe /T /F" in s and "$id = 5" in s for s in ps.scripts)


def test_capture_up_never_stops_a_foreign_capture_unasked(tmp_path):
    ps = FakePs([(7, r"arclog.exe capture --port COM9 --node c2 --out C:\mine --quiet")])
    with pytest.raises(RuntimeError, match="another capture holds the ports: pid 7 COM9"):
        cap(tmp_path, ps).up(["COM9"])
    assert not any("taskkill" in s or "Create" in s for s in ps.scripts)
    assert cap(tmp_path, ps).up(["COM9"], replace=True)[1] == "restarted"


# --- discovery and flash ---------------------------------------------------------

T0 = datetime(2026, 9, 29, 10, 0, tzinfo=timezone.utc)


def boot_lines(uid, build="dev", cls="C2", nid=2, cm4=True):
    lines = []
    if cm4:
        lines.append(f"000000T000000.9997 4S A #00 BOOT build={build}")
        lines.append("000000T000000.9997 4X M #01 CORE_SYNC stage=linked")
    lines.append(f"000101T000000.0031 0S A #00 BOOT cls={cls} id={nid} uid={format_uid(uid)} "
                 f"fw=1.5.0 build={build}")
    return lines


def write_capture(capture_dir, node, lines, start=T0):
    w = DailyWriter(capture_dir, node)
    for i, raw in enumerate(lines):
        w.write(start + timedelta(seconds=0.1 * i), raw)
    w.close()


class FakeProg:
    def __init__(self, probes, uids):
        self._probes, self.uids, self.calls = probes, uids, []

    def probes(self):
        return [Probe(sn, "NUCLEO-WL55JC") for sn in self._probes]

    def read_uid(self, sn):
        self.calls.append(("uid", sn))
        return self.uids[sn]

    def flash(self, sn, images):
        self.calls.append(("flash", sn, sorted(images)))


def test_the_last_boot_gives_uid_and_build(tmp_path):
    write_capture(tmp_path, "com9", boot_lines(UID1, build="old") + boot_lines(UID2, build="new"))
    assert last_boot(tmp_path, "com9") == (UID2, "new")
    assert last_boot(tmp_path, "com8") == (None, None)


def test_discovery_uses_the_trace_and_reads_swd_only_when_asked(tmp_path):
    write_capture(tmp_path, "com9", boot_lines(UID2))
    prog = FakeProg([SN8, SN9], {SN8: UID1})
    table = {UID1: 1, UID2: 2}
    boards = discover(prog, {SN8: "COM8", SN9: "COM9"}, table, tmp_path)
    assert [(b.port, b.node_id, b.uid_source) for b in boards] == [("COM8", None, ""), ("COM9", 2, "trace")]
    assert prog.calls == []
    boards = discover(prog, {SN8: "COM8", SN9: "COM9"}, table, tmp_path, probe_unknown=True)
    assert [(b.port, b.node_id, b.uid_source) for b in boards] == [("COM8", 1, "swd"), ("COM9", 2, "trace")]


def test_select_names_the_missing_nodes():
    boards = [Board(SN9, "COM9", UID2, "trace", 2), Board(SN8, "COM8")]
    assert select(boards, {2: "C2"}) == {2: boards[0]}
    with pytest.raises(LookupError, match="no connected board has Node ID 1; boards with an unknown UID: "
                                          f"{SN8} \\(try --probe-uids\\)"):
        select(boards, {1: "C3"})


def result(tmp_path):
    elfs = {(core, "Debug_C2"): tmp_path / f"{core}.elf" for core in ("CM4", "CM0PLUS")}
    return BuildResult("abc1234", ["Debug_C2"], True, BuildLog(), elfs)


def test_flash_checks_the_uid_over_swd_first(tmp_path):
    board = Board(SN9, "COM9", UID2, "trace", 2)
    prog = FakeProg([SN9], {SN9: UID2})
    flash_nodes(prog, {2: board}, {UID2: 2}, result(tmp_path), {2: "C2"}, report=lambda m: None)
    assert prog.calls == [("uid", SN9), ("flash", SN9, ["CM0PLUS", "CM4"])]


def test_a_board_moved_since_its_last_boot_is_not_flashed(tmp_path):
    board = Board(SN9, "COM9", UID2, "trace", 2)   # the trace says Node 2, the chip says Node 1
    prog = FakeProg([SN9], {SN9: UID1})
    with pytest.raises(LookupError, match="holds UID 0014008f3232501420383543, not Node ID 2"):
        flash_nodes(prog, {2: board}, {UID1: 1, UID2: 2}, result(tmp_path), {2: "C2"}, report=lambda m: None)
    assert prog.calls == [("uid", SN9)]


def test_boot_check_waits_through_the_old_image_then_passes(tmp_path):
    since = T0
    # The UID read reboots the old image, then the flash boots the new one.
    write_capture(tmp_path, "com9", boot_lines(UID2, build="dev") + boot_lines(UID2, build="abc1234"),
                  start=since + timedelta(seconds=1))
    board = Board(SN9, "COM9", UID2, "trace", 2)
    clock = iter([since + timedelta(seconds=s) for s in range(0, 100)])
    ok, verdict = boot_check(tmp_path, since, "abc1234", {2: board}, {2: "C2"}, timeout_s=30,
                             now=lambda: next(clock), sleep=lambda s: None, report=lambda m: None)
    assert ok, verdict


def test_boot_check_fails_on_the_wrong_class(tmp_path):
    write_capture(tmp_path, "com9", boot_lines(UID2, build="abc1234", cls="C3"), start=T0 + timedelta(seconds=1))
    board = Board(SN9, "COM9", UID2, "trace", 2)
    clock = iter([T0 + timedelta(seconds=s) for s in range(0, 100)])
    ok, verdict = boot_check(tmp_path, T0, "abc1234", {2: board}, {2: "C2"}, timeout_s=30,
                             now=lambda: next(clock), sleep=lambda s: None, report=lambda m: None)
    assert not ok and "cls=C3, expected C2" in verdict


def test_every_uid_is_checked_before_anything_is_flashed(tmp_path):
    boards = {1: Board(SN8, "COM8", UID1, "trace", 1), 2: Board(SN9, "COM9", UID2, "trace", 2)}
    prog = FakeProg([SN8, SN9], {SN8: UID1, SN9: UID1})       # the second board is not Node 2
    elfs = {(core, cfg): tmp_path / f"{core}.elf" for core in ("CM4", "CM0PLUS") for cfg in ("Debug_C2", "Debug_C3")}
    build = BuildResult("abc1234", ["Debug_C2", "Debug_C3"], True, BuildLog(), elfs)
    with pytest.raises(LookupError, match="nothing flashed"):
        flash_nodes(prog, boards, {UID1: 1, UID2: 2}, build, {1: "C3", 2: "C2"}, report=lambda m: None)
    assert [c[0] for c in prog.calls] == ["uid", "uid"]


def test_flash_returns_when_each_board_was_flashed(tmp_path):
    board = Board(SN9, "COM9", UID2, "trace", 2)
    clock = iter([T0, T0 + timedelta(seconds=7)])
    since, flashed = flash_nodes(FakeProg([SN9], {SN9: UID2}), {2: board}, {UID2: 2}, result(tmp_path),
                                 {2: "C2"}, report=lambda m: None, now=lambda: next(clock))
    assert since == T0 - timedelta(seconds=1) and flashed == {2: T0 + timedelta(seconds=7)}


def test_no_port_present_is_no_port():
    assert parse_ports("") == {}


def test_no_probe_connected_is_no_board(tmp_path):
    from bench.programmer import Programmer
    out = "      STM32CubeProgrammer v2.23.0\n\n===== STLink Interface =====\nNo ST-Link detected!\n"
    assert Programmer(exe="prog.exe", runner=lambda cmd: (0, out)).probes() == []


def test_a_failing_tool_is_one_line_not_a_traceback(monkeypatch, capsys):
    import subprocess
    from bench import cli

    def boom(args):
        raise subprocess.CalledProcessError(1, ["powershell.exe", "-Command", "x"], stderr="Get-PnpDevice : oops\nmore")
    monkeypatch.setattr(cli, "cmd_boards", boom)
    assert cli.main(["boards"]) == 1
    assert capsys.readouterr().err == "bench boards: powershell.exe failed (exit 1): Get-PnpDevice : oops\n"


def test_select_with_no_board_connected():
    with pytest.raises(LookupError, match="no ST-LINK probe is connected"):
        select([], {2: "C2"})
