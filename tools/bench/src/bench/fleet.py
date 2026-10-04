"""The fleet: every board bench can reach, behind the one interface it already calls (ADR-0002).

bench talks to a board through four calls keyed by a board id: probes(), read_uid(id), flash(id,
images) and reset(id). The id is a probe serial number for a board on a local ST-LINK
(`Programmer`) and the configured name for a board wired to a Pi Node (`PiNodeLink`). Discovery,
flashing and runs call this and do not know which kind they hold.
"""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Sequence

from bench.config import Remote
from bench.pinode import PI_NODE, PiNodeLink
from bench.programmer import Probe, Programmer, ProgrammerError


class Fleet:
    def __init__(self, local: Programmer, nodes: Sequence[PiNodeLink], reach_timeout: float = 2.0) -> None:
        self._local = local
        self._nodes = {n.remote.name: n for n in nodes}
        self._timeout = reach_timeout
        self._up: dict[str, bool] | None = None

    def _reachable(self) -> dict[str, bool]:
        """Which Pi Nodes answer, asked once per fleet and all at once (a node that is off costs the timeout)."""
        if self._up is None:
            nodes = list(self._nodes.values())
            with ThreadPoolExecutor(max_workers=max(1, len(nodes))) as pool:
                answers = list(pool.map(lambda n: n.log_reachable(self._timeout), nodes))
            self._up = {n.remote.name: ok for n, ok in zip(nodes, answers)}
        return self._up

    @property
    def down(self) -> list[Remote]:
        """Configured Pi Nodes that do not answer."""
        up = self._reachable()
        return [n.remote for name, n in self._nodes.items() if not up[name]]

    def probes(self) -> list[Probe]:
        up = self._reachable()
        return [*self._local.probes(), *(Probe(name, PI_NODE) for name in self._nodes if up[name])]

    def ports(self) -> dict[str, str]:
        """Board id -> capture source of every configured Pi Node (a node that is down is still named)."""
        return {name: n.remote.log_url for name, n in self._nodes.items()}

    def _node(self, board_id: str) -> PiNodeLink | None:
        node = self._nodes.get(board_id)
        if node is not None and not self._reachable()[board_id]:
            raise ProgrammerError(f"{board_id} ({node.remote.host}) is not answering")
        return node

    def read_uid(self, board_id: str) -> tuple[int, int, int]:
        node = self._node(board_id)
        return node.read_uid() if node else self._local.read_uid(board_id)

    def flash(self, board_id: str, images: dict[str, Path]) -> str:
        node = self._node(board_id)
        return node.flash(images) if node else self._local.flash(board_id, images)

    def reset(self, board_id: str) -> str:
        node = self._node(board_id)
        return node.reset() if node else self._local.reset(board_id)
