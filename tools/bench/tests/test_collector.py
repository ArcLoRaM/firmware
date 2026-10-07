"""bench.collector: the log collector's lines decide a scenario the way a capture does."""

import json
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

import pytest

from bench import collector
from bench.collector import (clock_warnings, convert_line, judge, node_number, validity_warnings,
                             write_capture)
from bench.scenario import from_dict

PARIS = ZoneInfo("Europe/Paris")
UTC = timezone.utc
SINCE = datetime(2026, 10, 7, 2, 28, 0, tzinfo=UTC)

# The first lines of nuna-node-03 after its reset on 2026-10-07, as the collector returns them
# (the Pi's local time, CEST): both cores boot as a C2 of build 474afcb, then it scans, takes a Sync
# packet and logs a slot every 20 s.
LOG = """2026-10-07 04:28:49.349199 | nuna-node-03 | 000000T000000.9997 4S A #00 BOOT build=474afcb
2026-10-07 04:28:49.399184 | nuna-node-03 | 000000T000000.9997 4X M #01 CORE_SYNC stage=wait_cm0
2026-10-07 04:28:49.449023 | nuna-node-03 | 000000T000000.9997 4X M #02 CORE_SYNC stage=cm0_up
2026-10-07 04:28:49.498963 | nuna-node-03 | 000000T000000.9997 4X M #03 CORE_SYNC stage=linked
2026-10-07 04:28:49.548797 | nuna-node-03 | 000000T000000.9997 4X M #04 CORE_SYNC stage=rtc_registered
2026-10-07 04:28:49.615240 | nuna-node-03 | 000101T000000.0034 0S A #00 BOOT cls=C2 id=2 uid=0026001a3232501420383543 fw=1.5.0 build=474afcb sync=DEV
2026-10-07 04:28:49.731776 | nuna-node-03 | 000101T000000.0302 0M L #01 MAC_INIT cls=C2 st=SCAN clk=COLD
2026-10-07 04:28:49.798269 | nuna-node-03 | 000101T000000.0346 0Y L #02 CALR req=9537 calp=1 calm=502 res=boot cal0=9537 trim=0
2026-10-07 04:28:49.881370 | nuna-node-03 | 000101T000000.0393 0T M #03 SCAN freq=868300000 why=boot
2026-10-07 04:28:49.947854 | nuna-node-03 | 000101T000000.0485 0S L #04 INIT_DONE phases=2
2026-10-07 04:30:53.531372 | nuna-node-03 | 000101T000204.1555 0P M #05 STOP2_WAKES n=64
2026-10-07 04:31:16.518459 | nuna-node-03 | 000101T000227.1376 0R M #06 RX_DONE sz=10 rssi=-4 snr=8 pre=146218 hdr=146806 rxd=147133 toa=991 st=146142
2026-10-07 04:31:16.618261 | nuna-node-03 | 000101T235822.0000 0Y L #07 RTC_SET old=147145 new=86301034 d=-246111 date=000101 shift=ok adv=147
2026-10-07 04:31:16.734661 | nuna-node-03 | 000101T235822.0000 0Y M #08 SYNC_RX ph=1 ce=0 ep=86300032 st=146142 exp=86300032 err=246111 erru=246110578 clk=COLD act=set
2026-10-07 04:31:16.850983 | nuna-node-03 | 000101T235822.0000 0Y L #09 CLK from=COLD to=ACQ why=rtc_set
2026-10-07 04:31:16.917500 | nuna-node-03 | 000101T235822.0000 0T M #0a BOOTSTRAP ph=1 ce=0 nom=86300032
2026-10-07 04:31:35.423266 | nuna-node-03 | 000101T235839.9335 0T H #0b SLOT ph=1 ty=SYNC ce=1 sl=0 pos=CELL dec=RX wake=0 nom=86320032
2026-10-07 04:31:35.506469 | nuna-node-03 | 000101T235839.9387 0T H #0c RX_WIN last=86321640 cap=86322632
2026-10-07 04:31:37.389852 | nuna-node-03 | 000101T235841.9028 0R H #0d RX_TIMEOUT pre=0
2026-10-07 04:31:55.423861 | nuna-node-03 | 000101T235859.9335 0T H #0e SLOT ph=1 ty=SYNC ce=2 sl=0 pos=CELL dec=RX wake=0 nom=86340032
2026-10-07 04:31:55.507063 | nuna-node-03 | 000101T235859.9387 0T H #0f RX_WIN last=86341640 cap=86342632
2026-10-07 04:31:57.390524 | nuna-node-03 | 000101T235901.9028 0R H #10 RX_TIMEOUT pre=0
2026-10-07 04:32:15.424464 | nuna-node-03 | 000101T235919.9335 0T H #11 SLOT ph=1 ty=SYNC ce=3 sl=0 pos=CELL dec=RX wake=0 nom=86360032
2026-10-07 04:32:15.507665 | nuna-node-03 | 000101T235919.9387 0T H #12 RX_WIN last=86361640 cap=86362632
2026-10-07 04:32:17.391533 | nuna-node-03 | 000101T235921.9033 0R H #13 RX_TIMEOUT pre=0
2026-10-07 04:32:29.615269 | nuna-node-03 | 000101T235934.1264 0P M #14 STOP2_WAKES n=128
2026-10-07 04:32:35.425072 | nuna-node-03 | 000101T235939.9335 0T H #15 SLOT ph=1 ty=SYNC ce=4 sl=0 pos=CELL dec=RX wake=0 nom=86380032
2026-10-07 04:32:35.508263 | nuna-node-03 | 000101T235939.9387 0T H #16 RX_WIN last=86381640 cap=86382632
2026-10-07 04:32:37.392262 | nuna-node-03 | 000101T235941.9033 0R H #17 RX_TIMEOUT pre=0
2026-10-07 04:32:55.424710 | nuna-node-03 | 000101T235959.9335 0T H #18 SLOT ph=1 ty=SYNC ce=5 sl=0 pos=CELL dec=RX wake=0 nom=32
2026-10-07 04:32:55.507927 | nuna-node-03 | 000101T235959.9377 0T H #19 RX_WIN last=1640 cap=2632
2026-10-07 04:32:57.390468 | nuna-node-03 | 000102T000001.9011 0R H #1a RX_TIMEOUT pre=0
2026-10-07 04:33:15.425573 | nuna-node-03 | 000102T000019.9335 0T H #1b SLOT ph=1 ty=SYNC ce=6 sl=0 pos=CELL dec=RX wake=0 nom=20032
2026-10-07 04:33:15.508803 | nuna-node-03 | 000102T000019.9382 0T H #1c RX_WIN last=21640 cap=22632"""
FIRST_SLOT = next(i for i, ln in enumerate(LOG.splitlines()) if " SLOT " in ln)
SCENARIO = {"nodes": {"2": "C2"}, "timeout": "20m",
            "expect": [{"node": 2, "event": "SLOT", "count": 3, "within": "5m"}]}
STATUS = {"nuna-node-03": {"connected": True, "connected_since": "2026-10-07T02:16:42+00:00",
                           "last_pi_timestamp": None, "last_line_received": None, "last_error": None}}


def collector_serving(monkeypatch, log: str, status: dict = STATUS) -> list[str]:
    asked: list[str] = []

    def http_get(url: str, timeout: float = 120.0) -> str:
        asked.append(url)
        return json.dumps(status) if url.endswith("/nodes") else log

    monkeypatch.setattr(collector, "http_get", http_get)
    return asked


def run_judge(tmp_path, now: datetime, log: str = LOG) -> tuple[int, list[str]]:
    said: list[str] = []
    code = judge(from_dict(SCENARIO), "474afcb", {2: "nuna-node-03"}, SINCE, "http://collector",
                 tmp_path, PARIS, now=now, report=said.append)
    return code, said


def test_a_collector_line_is_in_utc_with_its_node_and_device_line():
    node, t, device = convert_line(LOG.splitlines()[5], PARIS)
    assert node == "nuna-node-03"
    assert t == datetime(2026, 10, 7, 2, 28, 49, 615240, tzinfo=UTC)
    assert device.startswith("000101T000000.0034 0S A #00 BOOT cls=C2 id=2")


@pytest.mark.parametrize("text", ["", "# collector: connected", "nuna-node-03 | no time"])
def test_a_line_of_another_shape_is_skipped(text):
    assert convert_line(text, PARIS) is None


def test_capture_files_are_split_at_utc_midnight(tmp_path):
    text = ("2026-10-08 01:59:59.500000 | nuna-node-03 | 000102T000000.0000 0P M #00 STOP2_WAKES n=1\n"
            "2026-10-08 02:00:01.500000 | nuna-node-03 | 000102T000002.0000 0P M #01 STOP2_WAKES n=2\n")
    assert write_capture(text, tmp_path, PARIS) == {"nuna-node-03": 2}
    assert sorted(p.name for p in tmp_path.iterdir()) == ["nuna-node-03-20261007.log", "nuna-node-03-20261008.log"]
    assert (tmp_path / "nuna-node-03-20261007.log").read_text().startswith("2026-10-07T23:59:59.500000Z\t")


def test_node_numbers_come_from_the_name():
    assert node_number("nuna-node-03") == 3 and node_number("nuna-node-12") == 12
    with pytest.raises(ValueError):
        node_number("collector")


def test_a_run_whose_expectations_are_in_the_collector_passes(monkeypatch, tmp_path):
    asked = collector_serving(monkeypatch, LOG)
    code, said = run_judge(tmp_path, SINCE + timedelta(minutes=16))
    assert code == 0, said
    assert asked[0] == "http://collector/logs?nodes=3&since=2026-10-07T04:23:00"
    assert any("SLOT (3/3)" in m for m in said) and not any("SLOT (1/3)" in m for m in said)


def test_a_node_that_went_silent_fails_its_expectation_at_the_wall_clock(monkeypatch, tmp_path):
    before_the_first_slot = "\n".join(LOG.splitlines()[:FIRST_SLOT])
    collector_serving(monkeypatch, before_the_first_slot)
    code, said = run_judge(tmp_path, SINCE + timedelta(minutes=16), before_the_first_slot)
    assert code == 1
    assert any("SLOT" in m and "not within" in m for m in said)


def test_a_run_not_yet_due_has_no_verdict(monkeypatch, tmp_path):
    before_the_first_slot = "\n".join(LOG.splitlines()[:FIRST_SLOT])
    collector_serving(monkeypatch, before_the_first_slot)
    code, said = run_judge(tmp_path, SINCE + timedelta(minutes=2), before_the_first_slot)
    assert code == 2
    assert any(m.startswith("NO VERDICT YET") for m in said)


def test_a_collector_that_reconnected_after_the_start_is_flagged():
    nodes = {"nuna-node-03": {"connected": True, "connected_since": "2026-10-07T03:00:00+00:00"}}
    assert "lines may be missing" in validity_warnings(["nuna-node-03"], nodes, SINCE)[0]
    assert validity_warnings(["nuna-node-03"], STATUS, SINCE) == []


def test_a_node_the_collector_cannot_reach_is_flagged():
    nodes = {"nuna-node-03": {"connected": False, "last_error": "TimeoutError"}}
    assert "not connected" in validity_warnings(["nuna-node-03"], nodes, SINCE)[0]
    assert "does not know" in validity_warnings(["nuna-node-09"], nodes, SINCE)[0]


def test_a_pi_clock_in_another_zone_than_assumed_is_flagged():
    nodes = {"nuna-node-01": {"last_pi_timestamp": "2026-10-07 09:18:15.965231",
                              "last_line_received": "2026-10-07T07:18:16.089289+00:00"}}
    assert clock_warnings(nodes, PARIS) == []
    assert "times are shifted" in clock_warnings(nodes, UTC)[0]

