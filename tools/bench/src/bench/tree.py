"""The Build Tree: a synced copy of the repo on the Windows disk, built headless.

The copy keeps agent builds away from the developer's IDE, workspace and
Debug_C* folders, and the Windows compiler reads it at native speed instead
of over the \\\\wsl share.
"""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path

#: Repo top-level directories that are not part of the firmware build: never
#: synced, and not part of the Build ID either.
NOT_BUILT = ("tools", "Tests", "docs", ".claude")

#: Paths inside the Build Tree that belong to it, not to the repo: build
#: outputs (kept for incremental builds) and the generated override header
#: (kept so an unchanged header does not force a full rebuild).
TREE_OWNED = ("Debug_*/", "Release/", "/Common/Bench/bench_overrides.h")

OVERRIDES_H = Path("Common/Bench/bench_overrides.h")


@dataclass(frozen=True)
class BuildTree:
    root: Path          # the bench directory on C:, as a WSL path (/mnt/c/...)

    @property
    def tree(self) -> Path:
        return self.root / "tree"

    @property
    def workspace(self) -> Path:
        return self.root / "workspace"

    @property
    def overrides_h(self) -> Path:
        return self.tree / OVERRIDES_H

    def elf(self, core: str, config: str) -> Path:
        return self.tree / core / config / f"ArcLoRaM_Base_{core}.elf"


def rsync_command(repo: Path, tree: Path) -> list[str]:
    cmd = ["rsync", "-a", "--delete", "--exclude=.git/"]
    cmd += [f"--exclude=/{d}/" for d in NOT_BUILT]
    cmd += [f"--exclude={p}" for p in TREE_OWNED]
    return cmd + [f"{repo}/", f"{tree}/"]


def sync(repo: Path, tree: Path) -> None:
    """Mirror the repo's firmware sources into the Build Tree (uncommitted changes included)."""
    tree.mkdir(parents=True, exist_ok=True)
    subprocess.run(rsync_command(repo, tree), check=True)
