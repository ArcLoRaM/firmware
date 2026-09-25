"""End-to-end: the commands a user runs on capture files."""

from datetime import datetime, timezone

from arclog.capture import DailyWriter
from arclog.cli import main
from arclog.model import read_lines


def test_report_writes_markdown_and_csv(run_files, tmp_path, capsys):
    out = tmp_path / "report" / "run1.md"
    assert main(["report", *map(str, run_files), "-o", str(out), "--title", "Bench run 1"]) == 0
    assert out.read_text(encoding="utf-8").startswith("# Bench run 1")
    assert out.with_suffix(".csv").read_text(encoding="utf-8").startswith("node,")


def test_view_filters_by_event(run_files, capsys):
    c3_path, c2_path = run_files
    assert main(["view", str(c2_path), "--event", "CLK", "--no-color"]) == 0
    out = capsys.readouterr().out.strip().split("\n")
    assert len(out) == 2
    assert all(" CLK " in ln for ln in out)
    assert "to=WARM" in out[1]


def test_view_level_filter_hides_more_verbose_lines(run_files, capsys):
    _, c2_path = run_files
    main(["view", str(c2_path), "--level", "L", "--no-color"])
    out = capsys.readouterr().out
    assert " SYNC_RX " not in out  # M
    assert " CLK " in out           # L


def test_view_reports_lost_lines_even_when_hidden(tmp_path, capsys):
    f = tmp_path / "c2-20260925.log"
    f.write_text(
        "260925T120000.0000 0T H #01 SLOT ph=0 ty=SYNC ce=0 sl=0 pos=CELL dec=RX wake=0 nom=0\n"
        "260925T120003.0000 0T H #05 SLOT ph=0 ty=SYNC ce=1 sl=0 pos=CELL dec=RX wake=0 nom=3000\n"
        "260925T120003.1000 0Y L #06 CLK from=WARM to=COLD why=tier3\n",
        encoding="utf-8",
    )
    main(["view", str(f), "--event", "CLK", "--no-color"])
    out = capsys.readouterr().out
    assert "3 line(s) lost" in out and "hidden SLOT" in out


def test_merge_pairs_sync_packets(run_files, capsys):
    assert main(["merge", *map(str, run_files), "--event", "SYNC_RX", "--no-color"]) == 0
    out = capsys.readouterr().out.strip().split("\n")
    assert out and all("<- c3 tx#" in ln for ln in out)
    assert all(ln.startswith("c2") for ln in out)


def test_daily_writer_rotates_at_utc_midnight(tmp_path):
    w = DailyWriter(tmp_path, "c2")
    w.write(datetime(2026, 9, 25, 23, 59, 59, tzinfo=timezone.utc), "260925T235959.0000 0T H #01 SLOT")
    w.write(datetime(2026, 9, 26, 0, 0, 1, tzinfo=timezone.utc), "260926T000001.0000 0T H #02 SLOT")
    w.close()
    files = sorted(p.name for p in tmp_path.iterdir())
    assert files == ["c2-20260925.log", "c2-20260926.log"]
    (line,) = list(read_lines(tmp_path / "c2-20260926.log"))
    assert line.host_time == datetime(2026, 9, 26, 0, 0, 1, tzinfo=timezone.utc)
    assert line.seq == 2
