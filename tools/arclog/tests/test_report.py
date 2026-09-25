import math

import pytest

from arclog.model import read_lines
from arclog.report import (
    Stats,
    analyse,
    header_valid_ms,
    lora_symbol_ms,
    pooled_slope,
    to_csv,
    to_markdown,
)

from conftest import build_run


def streams(*logs):
    return {log.node: [ln for ln in _lines(log)] for log in logs}


def _lines(log):
    from arclog.model import parse_capture_line

    return [parse_capture_line(text, node=log.node, lineno=i) for i, text in enumerate(log.lines, 1)]


def node(run, name):
    return next(n for n in run.nodes if n.node == name)


def test_lora_timing_sf12_bw125():
    assert lora_symbol_ms(12, 125_000) == pytest.approx(32.768)
    assert header_valid_ms(12, 125_000, 8) == pytest.approx(20.25 * 32.768)


def test_stats():
    s = Stats.of([1, -3, 2])
    assert (s.n, s.min, s.max, s.p95_abs) == (3, -3, 2, 3)
    assert s.mean == pytest.approx(0.0)
    assert math.isnan(Stats.of([]).mean)


def test_pooled_slope_ignores_intercept_jumps_between_segments():
    # Same 2 ms/s slope, different offsets per segment (RTC corrections).
    segs = [[(0, 0), (1, 2), (2, 4)], [(3, 100), (4, 102)], [(9, 7)]]
    fit = pooled_slope(segs)
    assert fit.ppm == pytest.approx(2000.0)
    assert (fit.points, fit.segments) == (5, 2)


@pytest.mark.parametrize("ppm", [20.0, -35.0, 0.0])
def test_drift_is_recovered_from_a_synthetic_run(ppm):
    c3, c2 = build_run(drift_ppm=ppm, frames=200)
    rx = node(analyse(streams(c3, c2)), "c2")
    assert rx.drift.ppm == pytest.approx(ppm, abs=1.5)


def test_acquisition_tiers_and_corrections():
    c3, c2 = build_run(drift_ppm=20.0, frames=200)
    rx = node(analyse(streams(c3, c2)), "c2")
    assert len(rx.acquisitions) == 1
    acq = rx.acquisitions[0]
    assert acq.locked and acq.packets == 3
    assert rx.acts["set"] == 1 and rx.acts["good"] == 2
    assert rx.acts["t2"] == rx.rtc_shifts > 0
    assert sum(rx.losses.values()) == 0
    assert rx.warm_t1_err.p95_abs < 8


def test_preamble_latency_estimate():
    c3, c2 = build_run(hdr_minus_pre=530)
    run = analyse(streams(c3, c2))
    rx = node(run, "c2")
    assert rx.hdr_minus_pre.mean == pytest.approx(530)
    assert "Estimated preamble-detection latency: **133.6 ms**" in to_markdown(run)


def test_packets_are_paired_across_nodes():
    c3, c2 = build_run(drift_ppm=0.0, frames=10, tx_start_lag=5)
    run = analyse(streams(c3, c2))
    assert run.unmatched == 0
    assert run.matched == 3 + 9
    # Perfect clocks: receiver stamp = plan, sender radio start = plan + 5.
    assert run.pair_offset.mean == pytest.approx(-5)
    tx = node(run, "c3")
    assert tx.tx_latency.mean == pytest.approx(5)


def test_csv_has_one_row_per_received_packet(run_files):
    c3_path, c2_path = run_files
    run = analyse({"c3": list(read_lines(c3_path)), "c2": list(read_lines(c2_path))})
    rows = to_csv(run).strip().split("\n")
    assert rows[0].startswith("node,host_time")
    assert len(rows) - 1 == sum(len(n.rx) for n in run.nodes)
    assert all(r.split(",")[18] == "c3" for r in rows[1:])  # tx_node


def test_markdown_sections(run_files):
    c3_path, c2_path = run_files
    md = to_markdown(analyse({"c3": list(read_lines(c3_path)), "c2": list(read_lines(c2_path))}))
    for heading in ("## Trace health", "## Node c2", "## Node c3", "## Cross-node pairing"):
        assert heading in md
