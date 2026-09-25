from datetime import datetime, timezone

from arclog.model import Kind, level_allows, node_from_path, parse_capture_line, parse_line

# Real lines from a C3 bench run (after the %lu fix).
SLOT = "000101T000051.0012 0T H #55 SLOT ph=1 ty=SYNC ce=2 sl=0 pos=CELL dec=TX wake=-3 nom=51000"


def test_arclog_line_is_split_into_header_and_fields():
    ln = parse_line(SLOT)
    assert ln.kind is Kind.ARCLOG
    assert (ln.core, ln.mod, ln.level, ln.seq, ln.event) == ("0", "T", "H", 0x55, "SLOT")
    assert ln.fields["dec"] == "TX"
    assert ln.int("wake") == -3
    assert ln.int("nom") == 51000
    assert ln.dev_time == datetime(2000, 1, 1, 0, 0, 51, 1200)


def test_event_without_fields():
    ln = parse_line("260925T101010.0000 0R L #02 TX_TIMEOUT ")
    assert ln.kind is Kind.ARCLOG
    assert ln.event == "TX_TIMEOUT"
    assert ln.fields == {}


def test_legacy_line_keeps_device_time():
    ln = parse_line("260925T101010.5000 >CM0PLUS(Radio)")
    assert ln.kind is Kind.LEGACY
    assert ln.text == ">CM0PLUS(Radio)"
    assert ln.dev_time == datetime(2026, 9, 25, 10, 10, 10, 500000)


def test_raw_line():
    ln = parse_line("CM0PLUS : Radio registration done\r\n")
    assert ln.kind is Kind.RAW
    assert ln.text == "CM0PLUS : Radio registration done"


def test_invalid_calendar_date_gives_no_device_time():
    ln = parse_line("260000T101010.0000 0Y L #01 CLK from=WARM to=COLD why=tier3")
    assert ln.kind is Kind.ARCLOG
    assert ln.dev_time is None


def test_capture_prefix_sets_host_time():
    ln = parse_capture_line("2026-09-25T12:00:01.250000Z\t" + SLOT, node="c3")
    assert ln.host_time == datetime(2026, 9, 25, 12, 0, 1, 250000, tzinfo=timezone.utc)
    assert ln.node == "c3"
    assert ln.event == "SLOT"


def test_plain_serial_dump_without_prefix():
    ln = parse_capture_line(SLOT)
    assert ln.host_time is None
    assert ln.kind is Kind.ARCLOG


def test_malformed_field_value_is_not_an_int():
    ln = parse_line("260925T101010.0000 0Y M #01 SYNC_RX err=%d")
    assert ln.int("err") is None
    assert ln.get("err") == "%d"


def test_level_order():
    assert level_allows("A", "L")
    assert level_allows("M", "M")
    assert not level_allows("H", "M")


def test_node_from_file_name(tmp_path):
    assert node_from_path(tmp_path / "c3-20260925.log") == "c3"
    assert node_from_path(tmp_path / "bench.log") == "bench"
