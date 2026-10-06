"""bench choose: the page that asks which board runs as which class, and brings the answer back."""

import json
import re
import urllib.error
import urllib.request
from datetime import datetime, timezone

import pytest

from bench.boards import Board
from bench.choose import (AnswerError, Chooser, command_line, flags, open_command, render_chooser,
                          validate_answer)
from bench.config import Remote
from bench.viz import snapshot

UID = {n: (0x0014008F + n, 0x32325014, 0x20383543) for n in range(1, 8)}
NOW = datetime(2026, 10, 5, 9, 0, tzinfo=timezone.utc)


def snap():
    boards = [Board("SN-1", "COM8", UID[1], "trace", 1, "b1"),                       # free, local
              Board("SN-4", "COM10", UID[4], "trace", 4, "b1"),                      # free, recorded elsewhere
              Board("SN-9", "COM9"),                                                 # new: no Node ID
              Board("nuna-node-02", "tcp://nuna-node-02:4000/nuna-node-02", UID[2], "swd", 2, remote=True),
              Board("nuna-node-05", "tcp://nuna-node-05:4000/nuna-node-05", UID[5], "swd", 5, remote=True)]
    return snapshot(boards, [Remote("nuna-node-03", "nuna-node-03")], {"nuna-node-05": "wt-x: bench run y.toml (pid 7)"},
                    "wt", NOW, elsewhere={"SN-4": "82-fine-stamps"})


# --- the answer ---------------------------------------------------------------------------


def test_a_valid_answer_is_normalised():
    a = validate_answer(snap(), {"nodes": {"1": "C3", "2": "C2", "4": "watch", "7": "unused"},
                                 "overrides": {"TX_RAMP_MS": "5u"}, "note": "  first try \n"})
    assert a == {"nodes": {1: "C3", 2: "C2", 4: "watch"}, "overrides": {"TX_RAMP_MS": "5u"}, "note": "first try"}


def test_the_answer_becomes_the_flags_bench_run_takes():
    a = {"nodes": {4: "watch", 1: "C3", 2: "C2"}, "overrides": {"SYNC_TX_BUDGET": "1u", "BENCH_PROBE": "1"}, "note": ""}
    assert flags(a) == ["--node", "1=C3", "--node", "2=C2", "--watch", "4", "-D", "BENCH_PROBE=1", "-D", "SYNC_TX_BUDGET=1u"]
    assert command_line(a) == "--node 1=C3 --node 2=C2 --watch 4 -D BENCH_PROBE=1 -D SYNC_TX_BUDGET=1u"


@pytest.mark.parametrize("payload, problem", [
    ({"nodes": {}}, "at least one"),
    ({"nodes": {"1": "unused"}}, "at least one"),
    ({"nodes": {"1": "C4"}}, "class"),
    ({"nodes": {"3": "C2"}}, "not connected"),                 # nobody has Node ID 3
    ({"nodes": {"5": "C2"}}, "held by"),                       # held by another session
    ({"nodes": {"x": "C2"}}, "Node ID"),
    ("nodes", "object"),
    ({"nodes": {"1": "C3"}, "overrides": {"lower": "1"}}, "override"),
    ({"nodes": {"1": "C3"}, "overrides": {"X": "has space"}}, "override"),
    ({"nodes": {"1": "C3"}, "overrides": {"X": ""}}, "override"),
    ({"nodes": {"1": "C3"}, "overrides": ["X=1"]}, "overrides"),
])
def test_a_wrong_answer_says_what_is_wrong(payload, problem):
    with pytest.raises(AnswerError, match=problem):
        validate_answer(snap(), payload)


def test_every_problem_is_reported_not_only_the_first():
    with pytest.raises(AnswerError) as err:
        validate_answer(snap(), {"nodes": {"1": "C9", "5": "C2"}, "overrides": {"bad": "1"}})
    assert len(err.value.errors) == 3


# --- the page ---------------------------------------------------------------------------------


def page():
    return render_chooser(snap(), {1: "C3", 2: "C2"}, {"TX_RAMP_MS": "5u"})


def test_the_page_offers_a_class_for_each_free_board_and_the_recommendation_is_preselected():
    html = page()
    assert html.count("<select") == 3                           # Node 1, 2, 4
    selected = re.findall(r'<select[^>]*data-node="(\d+)"[^>]*>.*?<option value="([A-Za-z0-9]+)" selected', html, re.S)
    assert dict(selected) == {"1": "C3", "2": "C2", "4": "unused"}
    assert "TX_RAMP_MS=5u" in html


def test_a_held_new_or_unreachable_board_cannot_be_chosen_and_says_why():
    html = page()
    assert 'data-node="5"' not in html and 'data-node="3"' not in html
    assert "held by wt-x" in html and "nuna-node-03" in html
    assert "needs a Node ID" in html or "new board" in html
    assert "recorded by 82-fine-stamps" in html                   # selectable, with a warning


def test_the_page_is_self_contained_theme_aware_and_posts_to_a_relative_answer_url():
    html = page()
    assert not re.search(r"https?://", html)
    assert "prefers-color-scheme: dark" in html and "background: var(--bg)" in html
    assert 'fetch("answer"' in html


def test_text_from_boards_cannot_inject_markup_into_the_page():
    evil = snapshot([Board("n<script>x</script>", "COM8", UID[1], "trace", 1, '"><img src=x onerror=y>')], [],
                    {}, "wt<marquee>", NOW)
    html = render_chooser(evil, {}, {})
    assert not any(tag in html for tag in ("<script>x", "<img", "<marquee>"))


# --- the server ---------------------------------------------------------------------------------


@pytest.fixture
def chooser():
    c = Chooser(snap(), {1: "C3"}, {})
    c.start()
    yield c
    c.close()


def request(url, data=None, headers=None, method=None):
    req = urllib.request.Request(url, data=data, headers=headers or {}, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, r.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8")


def post(c, payload, **kw):
    headers = {"Content-Type": "application/json", **kw.pop("headers", {})}
    return request(c.url + "answer", data=json.dumps(payload).encode(), headers=headers, **kw)


def test_the_page_is_served_on_loopback_under_a_secret_path(chooser):
    assert chooser.url.startswith("http://127.0.0.1:") and len(chooser.url.rstrip("/").rsplit("/", 1)[1]) >= 16
    status, body = request(chooser.url)
    assert status == 200 and "<select" in body


def test_other_paths_and_a_wrong_token_find_nothing(chooser):
    base = chooser.url.rsplit("/", 2)[0]
    assert request(base + "/")[0] == 404
    assert request(base + "/wrong/")[0] == 404
    assert request(chooser.url + "../etc/passwd")[0] == 404


def test_a_request_with_another_host_name_is_refused(chooser):
    assert request(chooser.url, headers={"Host": "evil.example"})[0] == 403


def test_a_post_from_another_origin_is_refused(chooser):
    status, _ = post(chooser, {"nodes": {"1": "C3"}}, headers={"Origin": "http://evil.example"})
    assert status == 403 and chooser.wait(0.05) is None


def test_a_valid_answer_is_returned_to_the_waiting_agent(chooser):
    status, body = post(chooser, {"nodes": {"1": "C3", "2": "C2"}, "overrides": {}, "note": "go"})
    assert status == 200 and json.loads(body) == {"ok": True}
    assert chooser.wait(2) == {"nodes": {1: "C3", 2: "C2"}, "overrides": {}, "note": "go"}


def test_a_wrong_answer_gets_its_problems_back_and_the_agent_keeps_waiting(chooser):
    status, body = post(chooser, {"nodes": {"5": "C2"}})
    assert status == 400 and "held by" in json.loads(body)["errors"][0]
    assert chooser.wait(0.05) is None
    assert post(chooser, {"nodes": {"1": "C3"}})[0] == 200          # and a corrected one is accepted


def test_only_the_first_answer_counts(chooser):
    assert post(chooser, {"nodes": {"1": "C3"}})[0] == 200
    assert post(chooser, {"nodes": {"2": "C2"}})[0] == 409
    assert chooser.wait(1)["nodes"] == {1: "C3"}


def test_a_post_that_is_not_json_or_too_large_is_refused(chooser):
    assert request(chooser.url + "answer", data=b"nodes=1", headers={"Content-Type": "text/plain"})[0] == 415
    big = json.dumps({"nodes": {"1": "C3"}, "note": "x" * 100_000}).encode()
    assert request(chooser.url + "answer", data=big, headers={"Content-Type": "application/json"})[0] == 413


def test_waiting_for_nobody_times_out(chooser):
    assert chooser.wait(0.1) is None


# --- opening the page -----------------------------------------------------------------------------


def test_under_wsl_the_windows_browser_is_asked_to_open_it():
    cmd = open_command("http://127.0.0.1:5000/abc/", release="5.15.90.1-microsoft-standard-WSL2", platform="linux")
    assert cmd[:3] == ["powershell.exe", "-NoProfile", "-Command"] and "Start-Process 'http://127.0.0.1:5000/abc/'" in cmd[3]


def test_elsewhere_the_desktop_opener_is_used():
    assert open_command("http://x/", release="6.1.0", platform="linux") == ["xdg-open", "http://x/"]
    assert open_command("http://x/", release="23.1.0", platform="darwin") == ["open", "http://x/"]


def test_an_address_that_could_break_out_of_the_powershell_string_is_refused():
    with pytest.raises(ValueError):
        open_command("http://x/'; calc; '", release="microsoft", platform="linux")


# --- the recommendation and the whole loop ----------------------------------------------------------


def test_without_a_recommendation_the_c3_is_the_local_board_and_the_pi_nodes_are_c2():
    from bench.choose import default_proposal

    assert default_proposal(snap()) == {1: "C3", 2: "C2"}       # Node 4 is recorded by another session: not proposed


def test_no_local_board_means_no_default_c3():
    from bench.choose import default_proposal

    only_remote = snapshot([Board("nuna-node-02", "tcp://nuna-node-02:4000/nuna-node-02", UID[2], "swd", 2, remote=True)],
                           [], {}, "wt", NOW)
    assert default_proposal(only_remote) == {2: "C2"}


def test_asking_opens_the_page_and_returns_what_the_user_chose():
    import threading

    from bench.choose import ask

    said = []

    def user(url):                                  # the browser: the user picks and sends
        threading.Thread(target=lambda: post_to(url, {"nodes": {"1": "C3", "2": "C2"}, "overrides": {"A": "1"}, "note": ""})).start()
        return True

    answer = ask(snap(), {1: "C3"}, {}, timeout_s=5, opener=user, say=said.append)
    assert answer == {"nodes": {1: "C3", 2: "C2"}, "overrides": {"A": "1"}, "note": ""}
    assert said and said[0].startswith("http://127.0.0.1:")


def post_to(url, payload):
    request(url + "answer", data=json.dumps(payload).encode(), headers={"Content-Type": "application/json"})


def test_when_the_browser_cannot_be_opened_the_address_is_still_given_and_waiting_times_out():
    from bench.choose import ask

    said = []
    assert ask(snap(), {}, {}, timeout_s=0.2, opener=lambda url: False, say=said.append) is None
    assert any("open" in m.lower() for m in said) and any(m.startswith("http://127.0.0.1:") for m in said)


# --- the command line ---------------------------------------------------------------------------------


def test_choose_takes_a_recommendation_overrides_and_a_timeout():
    from bench.cli import build_parser

    a = build_parser().parse_args(["choose", "--propose", "1=C3", "--propose", "4=watch", "-D", "TX_RAMP_MS=5u",
                                   "--timeout", "5m", "--no-open"])
    assert (a.propose, a.define, a.timeout, a.no_open) == ([(1, "C3"), (4, "watch")], [("TX_RAMP_MS", "5u")], "5m", True)


@pytest.mark.parametrize("bad", ["C3", "1=C4", "x=C2", "1="])
def test_a_recommendation_needs_a_node_id_and_a_class(bad):
    from bench.cli import build_parser

    with pytest.raises(SystemExit):
        build_parser().parse_args(["choose", "--propose", bad])


def test_map_can_open_its_page():
    from bench.cli import build_parser

    assert build_parser().parse_args(["map", "--open"]).open is True
    assert build_parser().parse_args(["map"]).open is False
