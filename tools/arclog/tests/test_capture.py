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


class FakeSerialModule:
    """pyserial stand-in: the port fails `failures` times, then gives `data` and goes idle."""

    class SerialException(Exception):
        pass

    def __init__(self, failures, data=b""):
        self.failures = failures
        self.data = data
        mod = self

        class Serial:
            def __init__(self, port, baud, timeout):
                if mod.failures:
                    mod.failures -= 1
                    raise mod.SerialException(f"could not open port '{port}'")
                self.in_waiting = 0

            def __enter__(self):
                return self

            def __exit__(self, *exc):
                return False

            def read(self, n):
                chunk, mod.data = mod.data, b""
                return chunk

        self.Serial = Serial


def test_an_outage_is_logged_once_and_the_recovery_once(monkeypatch):
    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule(5, BOOT.encode() + b"\r\n"))
    monkeypatch.setattr(capture_mod, "RECONNECT_S", 0.001)
    logs = []
    lines = [raw for _, raw in capture_mod.serial_lines("COM6", log=logs.append, duration_s=0.3)]
    assert lines == [BOOT]
    assert [m.split(" (")[0] for m in logs] == ["arclog: COM6 unavailable", "arclog: listening on COM6 @ 9600"]


# --- a Pi Node's log over TCP ---------------------------------------------------------------------

PI_LINE = "2026-10-04 20:54:03.180082 | 000102T044054.0161 0T H #20 SLOT ph=1 ty=SYNC"
SLOT = "000102T044054.0161 0T H #20 SLOT ph=1 ty=SYNC"


class Peer:
    """A TCP server standing in for a Pi Node's log server: one script (byte chunks) per connection."""

    def __init__(self, *scripts):
        import socket
        import threading
        import time

        self.srv = socket.socket()
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(5)
        self.url = f"tcp://127.0.0.1:{self.srv.getsockname()[1]}/nuna-node-01"

        def serve():
            for chunks in scripts:
                conn, _ = self.srv.accept()
                for chunk in chunks:
                    conn.sendall(chunk)
                    time.sleep(0.02)
                conn.close()

        threading.Thread(target=serve, daemon=True).start()

    def close(self):
        self.srv.close()


@pytest.fixture
def peer_factory(monkeypatch):
    monkeypatch.setattr(capture_mod, "RECONNECT_S", 0.01)
    made = []

    def make(*scripts):
        made.append(Peer(*scripts))
        return made[-1]

    yield make
    for p in made:
        p.close()


def test_the_pi_stamp_is_removed_and_lines_are_rebuilt_across_chunks(peer_factory):
    peer = peer_factory([(PI_LINE + "\r\n").encode(),
                         b"2026-10-04 20:54:06.180128 | 000102T044057.0161 0T H #21 SL",
                         b"OT ph=1\n",
                         b"2026-10-04 20:54:07.000000 | \n",       # a stamp with nothing after it
                         b"not stamped\n"])
    lines = [raw for _, raw in capture_mod.tcp_lines(peer.url, log=lambda m: None, duration_s=1.0)]
    assert lines == [SLOT, "000102T044057.0161 0T H #21 SLOT ph=1", "not stamped"]


def test_a_line_is_stamped_with_the_controller_clock_in_utc(peer_factory):
    peer = peer_factory([(PI_LINE + "\n").encode()])
    before = datetime.now(timezone.utc)
    [(t, raw)] = list(capture_mod.tcp_lines(peer.url, log=lambda m: None, duration_s=0.6))
    assert before <= t <= datetime.now(timezone.utc) and t.tzinfo is not None
    assert raw == SLOT


def test_a_dropped_connection_is_retried(peer_factory):
    peer = peer_factory([(PI_LINE + "\n").encode()], [b"2026-10-04 20:54:09.000000 | 000102T044100.0163 0Y M #22 SYNC_EPOCH\n"])
    logs = []
    lines = [raw for _, raw in capture_mod.tcp_lines(peer.url, log=logs.append, duration_s=1.0)]
    assert lines == [SLOT, "000102T044100.0163 0Y M #22 SYNC_EPOCH"]
    assert sum("listening on" in m for m in logs) >= 2 and any("unavailable" in m for m in logs)


def test_without_reconnect_a_closed_connection_is_an_error(peer_factory):
    peer = peer_factory([(PI_LINE + "\n").encode()])
    with pytest.raises(ConnectionError, match="closed"):
        list(capture_mod.tcp_lines(peer.url, reconnect=False, log=lambda m: None, duration_s=2.0))


def test_capture_reads_a_tcp_port_with_the_tcp_source(tmp_path, peer_factory):
    peer = peer_factory([(PI_LINE + "\n").encode()])
    capture([(peer.url, "nuna-node-01")], tmp_path, duration_s=0.8)
    [f] = list(tmp_path.glob("nuna-node-01-*.log"))
    assert f.read_text(encoding="utf-8").rstrip("\n").endswith("\t" + SLOT)


@pytest.mark.parametrize("url", ["tcp://host", "tcp://:4000/x", "tcp://h:99999/x", "tcp://h:0/x", "COM6"])
def test_a_tcp_url_needs_a_host_and_a_port(url):
    with pytest.raises(ValueError, match="tcp://"):
        capture_mod.parse_tcp_url(url)


def test_a_tcp_url_gives_host_and_port_and_ignores_the_label():
    assert capture_mod.parse_tcp_url("tcp://100.64.0.11:4000/nuna-node-01") == ("100.64.0.11", 4000)
