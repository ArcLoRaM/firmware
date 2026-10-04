"""The bench map: which boards are connected, locally and on Pi Nodes, and whether they are free."""

import json
import re
from datetime import datetime, timezone

from bench.boards import Board
from bench.config import Remote
from bench.viz import render_html, snapshot

UID1 = (0x0014008F, 0x32325014, 0x20383543)
UID2 = (0x0026001A, 0x32325014, 0x20383543)
NOW = datetime(2026, 10, 4, 20, 30, tzinfo=timezone.utc)


def boards():
    return [Board("003D003D3234510833353533", "COM8", UID1, "trace", 1, "0fcb4fc-o599ce4"),
            Board("004D00303333511431363730", "COM10"),                       # a probe with no UID yet
            Board("nuna-node-02", "tcp://nuna-node-02:4000/nuna-node-02", UID2, "swd", 2, remote=True)]


def snap(held=None, down=()):
    return snapshot(boards(), list(down), held or {}, "78-bench-remote-nodes", NOW)


def test_the_snapshot_separates_local_probes_from_pi_nodes():
    s = snap()
    assert [(b["kind"], b["where"]) for b in s["boards"]] == [
        ("st-link", "COM8"), ("st-link", "COM10"), ("pi-node", "nuna-node-02:4000")]
    assert s["worktree"] == "78-bench-remote-nodes" and s["generated"] == "2026-10-04T20:30:00Z"


def test_a_board_is_free_new_or_held():
    s = snap(held={"003D003D3234510833353533": "82-fine-stamps: bench run x.toml (pid 7, since 19:43:20 UTC)"})
    status = {b["where"]: b["status"] for b in s["boards"]}
    assert status == {"COM8": "held", "COM10": "new", "nuna-node-02:4000": "free"}
    assert s["boards"][0]["held_by"].startswith("82-fine-stamps")


def test_the_snapshot_is_json_and_names_the_uid_as_the_firmware_logs_it():
    s = snap()
    assert json.loads(json.dumps(s)) == s
    assert s["boards"][0]["uid"] == "0014008f3232501420383543" and s["boards"][1]["uid"] is None


def test_pi_nodes_that_do_not_answer_are_listed_apart():
    s = snap(down=[Remote("nuna-node-01", "nuna-node-01")])
    assert s["not_answering"] == [{"id": "nuna-node-01", "where": "nuna-node-01:4000"}]
    assert [b["id"] for b in s["boards"]] == ["003D003D3234510833353533", "004D00303333511431363730", "nuna-node-02"]


def test_the_page_shows_every_board_with_its_node_id_and_state():
    html = render_html(snap(held={"003D003D3234510833353533": "82-fine-stamps: bench run x.toml (pid 7)"},
                            down=[Remote("nuna-node-01", "nuna-node-01")]))
    assert "<title>Bench Map</title>" in html
    for text in ("COM8", "COM10", "nuna-node-02", "nuna-node-01", "0fcb4fc-o599ce4", "82-fine-stamps"):
        assert text in html
    assert html.count('<article class="card"') == 4
    for state in ("free", "held", "new", "not answering"):
        assert f'data-state="{state}"' in html


def test_the_page_is_self_contained_and_follows_the_viewers_theme():
    html = render_html(snap())
    assert not re.search(r"https?://", html)
    assert "prefers-color-scheme: dark" in html and ':root[data-theme="dark"]' in html
    assert "background: var(--bg)" in html


def test_text_from_boards_and_hosts_cannot_inject_markup():
    evil = [Board("n<script>x</script>", "tcp://h:4000/n", UID1, "swd", 1, '"><img src=x onerror=y>', remote=True)]
    html = render_html(snapshot(evil, [], {"n<script>x</script>": "<blink>w</blink>"}, "wt<marquee>", NOW))
    assert not any(tag in html for tag in ("<script>", "<img", "<blink>", "<marquee>"))
    assert "&lt;script&gt;" in html and "&lt;blink&gt;" in html


def test_an_empty_bench_says_so_instead_of_showing_empty_columns():
    html = render_html(snapshot([], [], {}, "main", NOW))
    assert "No board connected" in html


def test_a_board_recorded_by_another_sessions_capture_says_so():
    s = snapshot(boards(), [], {}, "wt", NOW, elsewhere={"003D003D3234510833353533": "82-fine-stamps"})
    assert [b["capture_elsewhere"] for b in s["boards"]] == ["82-fine-stamps", None, None]
    html = render_html(s)
    assert "recorded by 82-fine-stamps" in html and html.count("recorded by") == 1


def test_a_captures_output_folder_names_its_worktree():
    from bench.viz import capture_owner

    wt = r"\\wsl.localhost\Ubuntu\home\simon\Projects\Firmware\.claude\worktrees\82-fine-stamps\tools\arclog\runs\bench"
    assert capture_owner(wt) == "82-fine-stamps"
    assert capture_owner(r"\\wsl.localhost\Ubuntu\home\simon\Projects\Firmware\tools\arclog\runs\bench") == "main"
    assert capture_owner(r"C:\elsewhere\out") == r"C:\elsewhere\out"
