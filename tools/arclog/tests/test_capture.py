"""capture: several ports in one process, each read and reconnected on its own."""

from datetime import datetime, timedelta, timezone

import pytest

import arclog.capture as capture_mod
from arclog.capture import capture
from arclog.cli import main

T0 = datetime(2026, 9, 29, 23, 59, 59, tzinfo=timezone.utc)
BOOT = "000000T000000.9997 4S A #00 BOOT build=dev"
SYNC = "000000T000001.0000 4X M #01 CORE_SYNC stage=linked"


def fake_ports(lines_by_port):
    """A line source replaying (seconds after T0, line) per port."""
    def source(port):
        for dt, raw in lines_by_port[port]:
            yield T0 + timedelta(seconds=dt), raw
    return source


def test_each_port_goes_to_its_node_file(tmp_path):
    src = fake_ports({"COM6": [(0, BOOT), (0.5, SYNC)], "COM8": [(0.2, BOOT)]})
    seen = []
    capture([("COM6", "c3"), ("COM8", "c2")], tmp_path, source=src,
            on_line=lambda ln: seen.append((ln.node, ln.event)))

    c3 = (tmp_path / "c3-20260929.log").read_text(encoding="utf-8").splitlines()
    c2 = (tmp_path / "c2-20260929.log").read_text(encoding="utf-8").splitlines()
    assert c3 == ["2026-09-29T23:59:59.000000Z\t" + BOOT, "2026-09-29T23:59:59.500000Z\t" + SYNC]
    assert c2 == ["2026-09-29T23:59:59.200000Z\t" + BOOT]
    assert sorted(seen) == [("c2", "BOOT"), ("c3", "BOOT"), ("c3", "CORE_SYNC")]


def test_files_rotate_at_utc_midnight_per_node(tmp_path):
    src = fake_ports({"COM6": [(0, BOOT), (2, SYNC)]})
    capture([("COM6", "c3")], tmp_path, source=src)
    assert (tmp_path / "c3-20260929.log").read_text(encoding="utf-8").endswith(BOOT + "\n")
    assert (tmp_path / "c3-20260930.log").read_text(encoding="utf-8").endswith(SYNC + "\n")


def test_a_quiet_port_does_not_hold_back_the_others(tmp_path):
    src = fake_ports({"COM6": [], "COM8": [(0, BOOT)]})
    capture([("COM6", "c3"), ("COM8", "c2")], tmp_path, source=src)
    assert not (tmp_path / "c3-20260929.log").exists()
    assert (tmp_path / "c2-20260929.log").exists()


def test_a_reader_error_is_raised_to_the_caller(tmp_path):
    def src(port):
        yield T0, BOOT
        raise RuntimeError(f"{port} broke")

    with pytest.raises(RuntimeError, match="COM6 broke"):
        capture([("COM6", "c3")], tmp_path, source=src)
    assert (tmp_path / "c3-20260929.log").exists()


@pytest.mark.parametrize("ports, problem", [
    ([], "no port"),
    ([("COM6", "c3"), ("COM6", "c2")], "port given twice"),
    ([("COM6", "c3"), ("COM8", "c3")], "node name given twice"),
])
def test_invalid_port_lists_are_refused(tmp_path, ports, problem):
    with pytest.raises(ValueError, match=problem):
        capture(ports, tmp_path, source=fake_ports({}))


def test_cli_pairs_ports_and_nodes_in_order(tmp_path, monkeypatch, capsys):
    calls = {}

    def fake_capture(ports, out_dir, **kwargs):
        calls["ports"] = ports

    monkeypatch.setattr(capture_mod, "capture", fake_capture)
    assert main(["capture", "--port", "COM6", "--node", "c3", "--port", "COM8", "--node", "c2",
                 "--out", str(tmp_path)]) == 0
    assert calls["ports"] == [("COM6", "c3"), ("COM8", "c2")]


def test_cli_refuses_unpaired_ports(tmp_path, capsys):
    assert main(["capture", "--port", "COM6", "--port", "COM8", "--node", "c3",
                 "--out", str(tmp_path)]) == 2
    assert "one --node per --port" in capsys.readouterr().err
