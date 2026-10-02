"""ArcLog event vocabulary.

Mirrors the ARCLOG() calls in the firmware. tests/test_schema.py scans the C
sources and fails when an event, its module or its keys drift from this table,
so the firmware and this tool cannot silently disagree.
"""

from __future__ import annotations

from dataclasses import dataclass

from arclog.model import Kind, Line


@dataclass(frozen=True)
class Event:
    mod: str
    keys: tuple[str, ...]
    doc: str
    optional: tuple[str, ...] = ()


EVENTS: dict[str, Event] = {
    # --- System / boot -------------------------------------------------------
    "BOOT": Event(
        "S",
        (),
        "Core booted, with the Build ID (\"dev\" outside the bench, see "
        "Common/Bench/bench_config.h). CM0+ adds node class, Node ID "
        "(0 = board not registered), MCU unique ID (w0 w1 w2) and firmware version.",
        ("cls", "id", "uid", "fw", "build"),
    ),
    "INIT_DONE": Event("S", ("phases",), "CM0+ protocol machines initialised; TDMA table phase count."),
    # --- Inter-core ----------------------------------------------------------
    "CORE_SYNC": Event("X", ("stage",), "CM4/CM0+ boot handshake stage."),
    # --- Power ---------------------------------------------------------------
    "STOP2_WAKES": Event("P", ("n",), "Every 64th STOP2 wake: total wakes since boot."),
    # --- MAC -----------------------------------------------------------------
    "MAC_INIT": Event("M", ("cls", "st", "clk"), "MAC reset to boot state."),
    "MAC_ST": Event("M", ("from", "to", "why"), "MacState transition."),
    # --- Sync / clock --------------------------------------------------------
    "CLK": Event("Y", ("from", "to", "why"), "ClockState transition (COLD/ACQ/WARM)."),
    "SYNC_RX": Event(
        "Y",
        ("ph", "ce", "ep", "st", "exp", "err", "clk", "act"),
        "Sync packet processed: stamp st vs expected exp, err = st - exp (ms). "
        "act: set (COLD RTC set), good/bad (ACQUIRING), t1/t2/t3 (WARM tier).",
    ),
    "SYNC_TX": Event(
        "Y",
        ("ph", "ce", "ep", "plan", "send", "freq"),
        "Sync packet handed to the radio: plan = nominal slot start (intended on-air start), send = RTC at Radio.Send.",
    ),
    "SYNC_EPOCH": Event("Y", ("ph", "ep"), "C3: Sync Phase Epoch captured at phase entry."),
    "SYNC_SILENCE": Event("Y", ("last", "now"), "Sync silence timeout expired (ADR-0013)."),
    "RTC_SET": Event("Y", ("old", "new", "d", "date", "shift"),
                     "Calendar RTC write; d = new - old (ms), adv = SHIFTR ticks (+ advance, - delay).",
                     optional=("adv",)),
    "RTC_SHIFT": Event(
        "Y", ("err", "ticks", "res"),
        "Tier 2 sub-second SHIFTR correction; res = ok | fail | busy (a previous shift still pending: not written).",
    ),
    "CALR": Event(
        "Y", ("req", "calp", "calm", "res"),
        "RTC smooth calibration write (#34); req = requested frequency change (ppb, + faster, - slower), "
        "calp/calm = the CALR fields written (32 s window), res = ok | fail | busy (a recalibration still pending "
        "(RECALPF): not written) | boot (the setting read from the register at boot, nothing written).",
    ),
    "DRIFT": Event(
        "Y", ("n", "base", "rate", "resid", "noise", "ok"),
        "Drift estimator state after a Sync sample (#34); n = samples in the window, base = baseline (s), "
        "rate = rate offset to cancel (ppb, + local clock fast), resid = rate + applied calibration (ppb), "
        "noise = sample sigma (us, 0 = not yet estimable), ok = 1 once the estimate may drive a CALR.",
    ),
    "SYNC_REJ": Event(
        "Y", ("ph", "ce"), "Sync packet dropped: ph is not a Sync phase (corrupt or foreign packet)."
    ),
    # --- TDMA ----------------------------------------------------------------
    "SLOT": Event(
        "T",
        ("ph", "ty", "ce", "sl", "pos", "dec", "wake", "nom"),
        "Slot wake: MAC decision; wake = actual - programmed wake (ms); nom = nominal slot start.",
    ),
    "SLOT_SUSPECT": Event("T", ("exp", "now"), "Wake far from expected: cursor untrusted, MAC re-acquires."),
    "CURSOR_CORRUPT": Event("T", ("pos",), "FrameCursor slot position corrupted."),
    "TX_DENIED": Event("T", ("res", "freq"), "Compliance engine refused a TX."),
    "TX_LATE": Event(
        "T", ("plan", "fire", "now"),
        "Slot task reached the fire instant (plan - Tx ramp) after it passed: a Sync packet is dropped, others go late.",
    ),
    "BOOTSTRAP": Event(
        "T", ("ph", "ce", "nom"), "Cursor re-anchored on a received Sync cell; the alarm chain (re)starts."
    ),
    "SCAN": Event(
        "T",
        ("freq", "why"),
        "Alarm chain stopped (CLOCK_COLD): continuous Rx on the discovery channel. why = boot or lost.",
    ),
    "RX_WIN": Event(
        "T",
        ("last", "cap"),
        "Synced Rx window opened: last = latest packet start (slot end + max guard - ToA), "
        "cap = hard end (slot end + max guard).",
    ),
    "RX_LATE": Event("T", ("now", "last"), "Woke after the latest packet start: no Rx window opened."),
    "WAKE_ADJ": Event(
        "T", ("from", "to"),
        "Next wake re-decided at Rx end (e.g. guard replaced by the Tx lead: next Sync cell became Tx).",
    ),
    # --- Radio ---------------------------------------------------------------
    "TX_DONE": Event("R", ("sz", "toa", "end", "start"), "TX finished at end; start = end - toa."),
    "TX_TIMEOUT": Event("R", (), "Radio TX timeout."),
    "RX_DONE": Event(
        "R",
        ("sz", "rssi", "snr", "pre", "hdr", "rxd", "toa", "st"),
        "Packet received. pre/hdr/rxd = RTC at PREAMBLE_DETECTED / HEADER_VALID / RX_DONE IRQ "
        "(0 = not seen); st = SyncStamp given to the MAC (rxd - toa, the packet start).",
    ),
    "RX_TIMEOUT": Event("R", ("pre",), "Rx window closed: no preamble by the latest packet start."),
    "RX_CAP": Event(
        "R",
        ("pre", "hdr"),
        "Reception still running at the window's hard end, aborted (false preamble detection, "
        "or a packet that started too late). pre/hdr = IRQ stamps, 0 = not seen.",
    ),
    "RX_ERROR": Event("R", ("pre", "hdr"), "CRC or header error."),
}


def validate(line: Line) -> list[str]:
    """Schema problems of an ARCLOG line (empty when valid or not ARCLOG)."""
    if line.kind is not Kind.ARCLOG:
        return []
    ev = EVENTS.get(line.event)
    if ev is None:
        return [f"unknown event {line.event}"]
    problems = []
    if line.mod != ev.mod:
        problems.append(f"{line.event}: module {line.mod}, expected {ev.mod}")
    keys = set(line.fields)
    missing = set(ev.keys) - keys
    extra = keys - set(ev.keys) - set(ev.optional)
    if missing:
        problems.append(f"{line.event}: missing {','.join(sorted(missing))}")
    if extra:
        problems.append(f"{line.event}: unexpected {','.join(sorted(extra))}")
    if line.event == "BOOT" and line.get("id") == "0":
        problems.append(unregistered_board(line.get("uid", "")))
    return problems


def unregistered_board(uid: str) -> str:
    """Problem text for a board missing from the Node ID table, with the entry to add."""
    words = [uid[i:i + 8] for i in range(0, 24, 8)] if len(uid) == 24 else []
    entry = ", ".join(f"0x{w.upper()}u" for w in words) if words else uid
    return (f"BOOT: board not registered, add {{ {{ {entry} }}, <next free id> }}, "
            "to Common/Protocol/node_id.c")
