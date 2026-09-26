# ArcLog structured trace and IRQ-entry SyncStamp

Both cores log through one structured line format, ArcLog (`Common/Log/arclog.h`): an RTC timestamp with the year, core, module, ST verbosity letter, per-core sequence number, event name and `key=value` fields.
A host tool (`tools/arclog/`) captures, classifies, merges and analyses the lines.
The sync timestamp given to the MAC (SyncStamp) is the packet start on air: the RTC at entry of the RxDone radio IRQ minus the packet's airtime, no longer the raw RxDone callback time.

## Context

Logging had no rule: free-text tags per file, timestamps on some lines only, enums as numbers, nothing machine-readable.
The C3 to C2 sync work (issue #21) needs per-packet evidence (stamp, expected arrival, error, decision, every RTC correction) from two nodes on separate serial ports, over runs of weeks to months.

While instrumenting, the RxDone timestamp turned out to lag the packet start by one airtime (~1 s for a SyncPayload at SF12).
Every node stamped the same way, so errors looked like zero while the whole receiver domain ran one airtime behind the sender.

## Decision

- **Text, not binary.** One line per event, readable in a plain terminal, parsed by one regex.
  The 9600-baud link carries ~11–15 lines/s, far above the sync event rate (one Sync packet every few seconds to hours), so compactness does not justify a binary format and its decoder dependency.
- **Loss is detected, not prevented.** A per-core 8-bit sequence number, consumed only by lines that pass the verbosity filter, exposes any FIFO overflow on either core as a gap.
- **Timestamp `YYMMDDTHHMMSS.ssss`.** It carries the date so months-long runs stay unambiguous; `UTIL_ADV_TRACE_TMP_MAX_TIMESTMAP_SIZE` is raised to 20 in a USER CODE region.
  The host capture adds its own UTC time per line to relate device time to wall time and to spot RTC jumps.
- **ST verbosity letters** (`A L M H` for `VLEVEL_ALWAYS/L/M/H`), so runtime filtering is unchanged.
- **Cross-node pairing by protocol key** `(ep, ph, ce)`, which both the sender (`SYNC_TX`) and the receiver (`SYNC_RX`) log, so the two captures need no shared clock.
- **Logs are tested.** Host unit tests capture ARCLOG lines and assert on them; a tool test fails when the firmware's `ARCLOG()` calls and the tool's event schema disagree, or when a format uses a length modifier the target formatter does not support.
- **SyncStamp = RxDone − ToA, stamped at IRQ entry.** `SUBGHZ_Radio_IRQHandler` stamps the RTC before HAL dispatch; the SyncStamp is the RxDone stamp minus `Radio.TimeOnAir` of the received length, minus `RX_DONE_LATENCY_MS` (sub-millisecond, 0 until measured).
  Because the MAC runs at RxDone, an RTC write (Packet 1, Tier 3) carries the time elapsed since the stamp, so the new domain reads the sender's nominal time at the stamp instant.
  `IRQ_PREAMBLE_DETECTED` and `IRQ_HEADER_VALID` are still enabled and stamped, and logged in `RX_DONE` as diagnostics.

## Considered Options

- **Binary frames (COBS, Trice/defmt style):** 5–10× smaller, but unreadable without the decoder and needs a new MbMux payload path.
  Rejected: bandwidth is not the constraint at the sync event rate.
- **Raise the baud rate:** not needed for the event rate; kept at 9600.
- **PREAMBLE_DETECTED as the stamp:** the earliest IRQ of a reception, and the first choice of this ADR.
  Rejected after the C3-C2 bench (2026-09-26): at SF12/BW125 the detection lands one symbol (32.8 ms) early or late from packet to packet, while `RxDone − ToA` and `HEADER_VALID` of the same packets agree with the sender's timing to ~3 ms.
  The jitter exceeds `SYNC_PARTICIPATE_THRESHOLD_MS` (8 ms), so it drove spurious Tier-2 corrections and could leave acquisition anchored on an outlier.
- **HEADER_VALID as the stamp:** as stable as `RxDone − ToA`, but it does not exist once the Sync packet uses implicit header (issue #39).
  Logged as a diagnostic instead.

## Consequences

- ST-generated trace lines outside CubeMX USER CODE regions cannot be converted; the tool classifies them as LEGACY.
- New events must be added to `tools/arclog/src/arclog/schema.py` in the same change as the firmware.
