"""bench.toml: where bench looks for Pi Nodes."""

import pytest

from bench.config import Remote, load_remotes, parse_config


def test_a_remote_has_defaults_for_the_ports_and_the_name():
    [r] = parse_config('[[remote]]\nhost = "nuna-node-01"\n')
    assert r == Remote(name="nuna-node-01", host="nuna-node-01", gdb=3333, log=4000)


def test_a_remote_names_its_capture_node_in_the_log_url():
    [r] = parse_config('[[remote]]\nname = "nuna-node-02"\nhost = "100.64.0.12"\ngdb = 3340\nlog = 4010\n')
    assert (r.name, r.host, r.gdb, r.log) == ("nuna-node-02", "100.64.0.12", 3340, 4010)
    assert r.log_url == "tcp://100.64.0.12:4010/nuna-node-02"


def test_remotes_keep_the_file_order():
    names = [r.name for r in parse_config('[[remote]]\nhost = "b-node"\n[[remote]]\nhost = "a-node"\n')]
    assert names == ["b-node", "a-node"]


def test_no_remote_is_an_empty_list():
    assert parse_config("") == []


@pytest.mark.parametrize("text, why", [
    ('[[remote]]\nhost = "a"\n[[remote]]\nhost = "a"\n', "twice"),
    ('[[remote]]\nname = "Node One"\nhost = "a"\n', "name"),
    ('[[remote]]\nhost = "100.1.2.3"\n', "name"),               # an address is not a capture name
    ('[[remote]]\nname = "a"\n', "host"),
    ('[[remote]]\nname = "a"\nhost = "x y"\n', "host"),
    ('[[remote]]\nname = "a"\nhost = "x;rm"\n', "host"),
    ('[[remote]]\nname = "a"\nhost = "h:22"\n', "host"),
    ('[[remote]]\nname = "a"\nhost = "h"\ngdb = 0\n', "gdb"),
    ('[[remote]]\nname = "a"\nhost = "h"\nlog = 70000\n', "log"),
    ('[[remote]]\nname = "a"\nhost = "h"\nlog = "4000"\n', "log"),
    ('[[remote]]\nname = "a"\nhost = "h"\nssh = "pi"\n', "unknown key"),
    ('[[remotes]]\nhost = "h"\n', "unknown"),
])
def test_a_wrong_config_says_what_is_wrong(text, why):
    with pytest.raises(ValueError, match=why):
        parse_config(text)


def test_the_environment_names_the_config_file(tmp_path):
    f = tmp_path / "x.toml"
    f.write_text('[[remote]]\nhost = "nuna-node-01"\n', encoding="utf-8")
    assert [r.name for r in load_remotes(env={"BENCH_CONFIG": str(f)}, home=tmp_path)] == ["nuna-node-01"]


def test_the_default_file_is_in_the_user_config_directory(tmp_path):
    f = tmp_path / ".config" / "bench" / "bench.toml"
    f.parent.mkdir(parents=True)
    f.write_text('[[remote]]\nhost = "nuna-node-02"\n', encoding="utf-8")
    assert [r.name for r in load_remotes(env={}, home=tmp_path)] == ["nuna-node-02"]


def test_no_default_file_means_no_remote(tmp_path):
    assert load_remotes(env={}, home=tmp_path) == []


def test_a_named_file_that_is_missing_is_an_error(tmp_path):
    with pytest.raises(ValueError, match="BENCH_CONFIG"):
        load_remotes(env={"BENCH_CONFIG": str(tmp_path / "nope.toml")}, home=tmp_path)
