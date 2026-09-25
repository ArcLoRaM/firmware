from arclog.health import SeqTracker, check
from arclog.model import parse_capture_line, parse_line


def seq_line(seq: int, core: str = "0", event: str = "SLOT") -> str:
    return f"260925T101010.0000 {core}T H #{seq:02x} {event}"


def feed(tracker, *seqs, core="0"):
    return [tracker.feed(parse_line(seq_line(s, core), node="c2")) for s in seqs]


def test_consecutive_sequence_has_no_loss():
    assert [r.lost for r in feed(SeqTracker(), 1, 2, 3)] == [0, 0, 0]


def test_gap_counts_lost_lines():
    assert [r.lost for r in feed(SeqTracker(), 1, 2, 6)] == [0, 0, 3]


def test_wrap_at_256_is_not_a_loss():
    assert [r.lost for r in feed(SeqTracker(), 0xFE, 0xFF, 0x00, 0x01)] == [0, 0, 0, 0]


def test_small_backward_step_is_reordering():
    results = feed(SeqTracker(), 10, 12, 11, 13)
    assert results[1].lost == 1
    assert results[2].reordered
    assert results[3].lost == 0


def test_cores_are_tracked_separately():
    t = SeqTracker()
    feed(t, 1, core="0")
    feed(t, 50, core="4")
    assert feed(t, 2, core="0")[0].lost == 0


def test_boot_resets_the_sequence():
    t = SeqTracker()
    feed(t, 0x40)
    assert t.feed(parse_line(seq_line(0x00, event="BOOT"), node="c2")).lost == 0
    assert feed(t, 1)[0].lost == 0


def test_rtc_jump_is_device_step_not_explained_by_host_step():
    lines = [
        parse_capture_line("2026-09-25T12:00:00.000000Z\t260925T120000.0000 0T H #01 SLOT", "c2"),
        # 30 s later on both clocks: quiet period, not a jump
        parse_capture_line("2026-09-25T12:00:30.000000Z\t260925T120030.0000 0T H #02 SLOT", "c2"),
        # 1 s later on the host, 1 h later on the device: RTC written
        parse_capture_line("2026-09-25T12:00:31.000000Z\t260925T130031.0000 0T H #03 SLOT", "c2"),
    ]
    h = check(lines)
    assert len(h.rtc_jumps) == 1
    assert h.rtc_jumps[0][0] is lines[2]
