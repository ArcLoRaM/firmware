"""What the worktrees that use the bench at once share, and what each has of its own (ADR-0003).

Shared, in the main checkout: the always-on capture and the run records (`tools/arclog/runs/`),
so every session reads every board's trace whoever started the capture, and a record survives
its worktree. Of its own, per worktree: the Build Tree, because a build rewrites it.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

RUNS = Path("tools/arclog/runs")


def _git_common_dir(path: Path) -> Path:
    common = subprocess.run(["git", "-C", str(path), "rev-parse", "--git-common-dir"],
                            check=True, capture_output=True, text=True).stdout.strip()
    return (Path(path) / common).resolve()


def shared_root(path: Path) -> Path:
    """The main checkout of the repository containing `path` (the path itself outside a worktree)."""
    git_dir = _git_common_dir(path)
    return git_dir.parent if git_dir.name == ".git" else Path(path).resolve()


def lease_dir(path: Path) -> Path:
    """Where board leases are taken: inside the repository's own git folder, which every worktree shares
    and git never commits."""
    return _git_common_dir(path) / "bench-leases"


def worktree_name(repo: Path) -> str:
    """'main' for the main checkout, else the worktree's folder name (`.claude/worktrees/<name>`)."""
    repo = Path(repo).resolve()
    return "main" if repo == shared_root(repo) else repo.name


def default_root(repo: Path, base: Path) -> Path:
    """The Build Tree root of a checkout: `base` for the main checkout, `<base>-<name>` for a worktree."""
    name = worktree_name(repo)
    return base if name == "main" else base.with_name(f"{base.name}-{name}")


def ensure_runs_link(repo: Path) -> None:
    """Make `tools/arclog/runs` of a worktree lead to the shared one, so the paths in the docs hold
    (`tools/arclog/runs/bench/<node>-YYYYMMDD.log`). A folder that already holds files is left alone."""
    repo = Path(repo).resolve()
    shared = shared_root(repo)
    if shared == repo:
        return
    target, link = shared / RUNS, repo / RUNS
    if link.is_symlink() or (link.exists() and any(link.iterdir())):
        return
    target.mkdir(parents=True, exist_ok=True)
    if link.exists():
        link.rmdir()
    link.parent.mkdir(parents=True, exist_ok=True)
    link.symlink_to(target)
