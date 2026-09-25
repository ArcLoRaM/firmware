"""Cross-node timeline and Sync packet pairing.

Two nodes are captured on separate serial ports, so their lines only share
the host clock. Sync packets are additionally matched exactly by the
protocol key (ep, ph, ce): the epoch, phase and cell carried in the
SyncPayload, logged by the sender (SYNC_TX) and by the receiver (SYNC_RX).
"""

from __future__ import annotations

import heapq
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Iterable

from arclog.model import Kind, Line

PacketKey = tuple[int, int, int]  # (ep, ph, ce)

_EPOCH0 = datetime.min.replace(tzinfo=timezone.utc)


def merge(streams: Iterable[list[Line]]) -> list[Line]:
    """Interleave per-node streams by host time (stable within a stream)."""
    keyed = [
        [((ln.host_time or _EPOCH0), i, n, ln) for n, ln in enumerate(stream)]
        for i, stream in enumerate(streams)
    ]
    return [item[3] for item in heapq.merge(*keyed)]


def packet_key(line: Line) -> PacketKey | None:
    ep, ph, ce = line.int("ep"), line.int("ph"), line.int("ce")
    if ep is None or ph is None or ce is None:
        return None
    return (ep, ph, ce)


@dataclass
class TxRecord:
    """A transmitted Sync packet: SYNC_TX plus the TX_DONE that followed it."""

    node: str
    tx: Line
    done: Line | None = None

    @property
    def plan(self) -> int | None:
        return self.tx.int("plan")

    @property
    def start(self) -> int | None:
        """Radio TX start in the sender's clock (TX_DONE end - toa)."""
        return self.done.int("start") if self.done else None


@dataclass
class RxRecord:
    """A received Sync packet: RX_DONE plus the SYNC_RX the MAC logged for it."""

    node: str
    rx: Line
    radio: Line | None = None

    def field(self, key: str) -> int | None:
        return self.radio.int(key) if self.radio else None


def tx_records(lines: list[Line]) -> list[TxRecord]:
    """Pair each SYNC_TX with the next TX_DONE of the same node and core."""
    out: list[TxRecord] = []
    pending: dict[str, TxRecord] = {}
    for ln in lines:
        if ln.kind is not Kind.ARCLOG or ln.core != "0":
            continue
        if ln.event == "SYNC_TX":
            rec = TxRecord(ln.node, ln)
            pending[ln.node] = rec
            out.append(rec)
        elif ln.event == "TX_DONE" and ln.node in pending:
            pending.pop(ln.node).done = ln
    return out


def rx_records(lines: list[Line]) -> list[RxRecord]:
    """Pair each SYNC_RX with the RX_DONE that immediately preceded it."""
    out: list[RxRecord] = []
    last_radio: dict[str, Line] = {}
    for ln in lines:
        if ln.kind is not Kind.ARCLOG or ln.core != "0":
            continue
        if ln.event == "RX_DONE":
            last_radio[ln.node] = ln
        elif ln.event == "SYNC_RX":
            out.append(RxRecord(ln.node, ln, last_radio.pop(ln.node, None)))
    return out


def pair(txs: list[TxRecord], rxs: list[RxRecord]) -> dict[int, TxRecord]:
    """Map id(RxRecord.rx) -> the TxRecord of another node with the same key.

    When several transmissions share a key (a retransmitted epoch), the most
    recent one before the reception (by host time) wins.
    """
    by_key: dict[PacketKey, list[TxRecord]] = {}
    for t in txs:
        k = packet_key(t.tx)
        if k is not None:
            by_key.setdefault(k, []).append(t)
    out: dict[int, TxRecord] = {}
    for r in rxs:
        k = packet_key(r.rx)
        cands = [t for t in by_key.get(k, []) if t.node != r.node] if k else []
        if not cands:
            continue
        if r.rx.host_time is not None:
            before = [t for t in cands if t.tx.host_time and t.tx.host_time <= r.rx.host_time]
            cands = before or cands
        out[id(r.rx)] = cands[-1]
    return out


def pair_notes(lines: list[Line]) -> dict[int, str]:
    """Display notes for merged output: which TX each SYNC_RX came from."""
    txs, rxs = tx_records(lines), rx_records(lines)
    pairs = pair(txs, rxs)
    notes = {}
    for r in rxs:
        t = pairs.get(id(r.rx))
        if t is None:
            notes[id(r.rx)] = "<- no matching TX"
            continue
        st, start = r.rx.int("st"), t.start
        delta = f" st-start={st - start}ms" if st is not None and start is not None else ""
        notes[id(r.rx)] = f"<- {t.node} tx#{t.tx.seq:02x}{delta}"
    return notes
