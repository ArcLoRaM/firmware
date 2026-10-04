"""Several worktrees use the bench at once (ADR-0003): what they share and what each has of its own."""

import subprocess
from pathlib import Path

import pytest

from bench.session import default_root, ensure_runs_link, shared_root, worktree_name

BASE = Path("/mnt/c/Users/Simon/arcfw-bench")


def git(cwd, *args):
    subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args], cwd=cwd, check=True,
                   capture_output=True)


@pytest.fixture
def repos(tmp_path):
    main = tmp_path / "Firmware"
    main.mkdir()
    git(main, "init", "-b", "main")
    git(main, "commit", "--allow-empty", "-m", "root")
    wt = tmp_path / "Firmware" / ".claude" / "worktrees" / "78-remote"
    git(main, "worktree", "add", "-b", "feat/78", str(wt))
    return main.resolve(), wt.resolve()


def test_the_main_checkout_is_its_own_shared_root(repos):
    main, _ = repos
    assert shared_root(main) == main
    assert worktree_name(main) == "main"


def test_a_worktree_and_its_subdirectories_share_the_main_checkouts_root(repos):
    main, wt = repos
    (wt / "tools" / "bench").mkdir(parents=True)
    assert shared_root(wt) == main
    assert shared_root(wt / "tools" / "bench") == main
    assert worktree_name(wt) == "78-remote"


def test_each_worktree_builds_in_a_tree_of_its_own(repos):
    main, wt = repos
    assert default_root(main, BASE) == BASE
    assert default_root(wt, BASE) == Path("/mnt/c/Users/Simon/arcfw-bench-78-remote")


def test_a_worktree_reaches_the_shared_runs_through_its_own_runs_folder(repos):
    main, wt = repos
    (main / "tools" / "arclog" / "runs" / "bench").mkdir(parents=True)
    ensure_runs_link(wt)
    link = wt / "tools" / "arclog" / "runs"
    assert link.is_symlink() and link.resolve() == main / "tools" / "arclog" / "runs"
    ensure_runs_link(wt)                                   # idempotent
    assert link.resolve() == main / "tools" / "arclog" / "runs"


def test_a_runs_folder_that_already_holds_files_is_left_alone(repos):
    main, wt = repos
    own = wt / "tools" / "arclog" / "runs"
    own.mkdir(parents=True)
    (own / "keep.txt").write_text("x")
    ensure_runs_link(wt)
    assert not own.is_symlink() and (own / "keep.txt").exists()


def test_the_main_checkout_needs_no_link(repos):
    main, _ = repos
    ensure_runs_link(main)
    assert not (main / "tools" / "arclog" / "runs").is_symlink()


def test_every_worktree_takes_its_leases_in_the_same_folder(repos):
    from bench.session import lease_dir

    main, wt = repos
    assert lease_dir(main) == lease_dir(wt) == main / ".git" / "bench-leases"
