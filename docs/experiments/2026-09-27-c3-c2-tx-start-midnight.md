# C3-C2 bench 2026-09-27: #44 Tx start (no prepare, TX_RAMP_MS 0) and #54 midnight

Firmware: `cd3ec00` + `BENCH_RTC_START_S=86100` on C3 + Tx prepare step disabled (the build that became `2bca7b8`, before `TX_RAMP_MS = 4`).
Boards: C3 on COM9, C2 on a new board on COM8; capture from 2026-09-27 22:27:30 UTC. RTC midnight crossed at 22:32:30 UTC.
Result: every packet on air 4 ms after its nominal start in every cell, on both nodes (-> `TX_RAMP_MS = 4`); midnight crossed with no `SLOT_SUSPECT`, no drop to `CLOCK_COLD`, C2 `SYNC_RX err=0` at 00:00:00.026.
A third board (COM6) failed every Tx (`TX_TIMEOUT`, no `TX_DONE`) and every Rx with both firmware images: faulty, excluded.

Radio: SF12, BW 125 kHz, 8-symbol preamble (symbol 32.768 ms, nominal HEADER_VALID at 663.6 ms after TX start).

## Trace health

| node | lines | ArcLog | legacy | raw | lost lines | reordered | schema problems | RTC jumps | boots |
|---|---|---|---|---|---|---|---|---|---|
| c2 | 630 | 630 | 0 | 0 | 1 | 0 | 0 | 1 | 2 |
| c3 | 358 | 357 | 1 | 0 | 12 | 0 | 0 | 2 | 3 |

## Node c2

Sync packets sent: 39. TX_LATE: 0.

Radio TX start minus nominal slot start (ms), 0 in every cell when the Tx start is deterministic (ADR-0016):

| n | mean | stdev | min | max | p95 abs |
|---|---|---|---|---|---|
| 39 | 4.05 | 0.32 | 4 | 6 | 4 |

| ce | n | mean | stdev | min | max | p95 abs |
|---|---|---|---|---|---|---|
| 1 | 13 | 4.15 | 0.55 | 4 | 6 | 6 |
| 2 | 13 | 4.00 | 0.00 | 4 | 4 | 4 |
| 3 | 13 | 4.00 | 0.00 | 4 | 4 | 4 |

Tx ramp, radio TX start minus Radio.Send (ms), the platform's TX_RAMP_MS:

| n | mean | stdev | min | max | p95 abs |
|---|---|---|---|---|---|
| 39 | 4.05 | 0.32 | 4 | 6 | 4 |

Sync packets received: 17. Acquisitions: 1 (1 reached CLOCK_WARM).

| # | packets to WARM | seconds | outcome |
|---|---|---|---|
| 1 | 3 | 6.0 | WARM |

Drops to CLOCK_COLD: 0 (none). RTC_SET: 1. RTC_SHIFT: 0. SLOT_SUSPECT: 0.

Decisions: set=1 good=2 t1=14.

Error while CLOCK_WARM, err = stamp - expected (ms):

| packets | n | mean | stdev | min | max | p95 abs |
|---|---|---|---|---|---|---|
| all tiers | 14 | -0.43 | 1.22 | -2 | 2 | 2 |
| tier 1 | 14 | -0.43 | 1.22 | -2 | 2 | 2 |

Drift: **8.18 ppm** from 14 points in 1 correction-free segments spanning 0.14 h.

PREAMBLE_DETECTED minus SyncStamp (ms, diagnostic):

| n | mean | stdev | min | max | p95 abs |
|---|---|---|---|---|---|
| 17 | 75.71 | 9.88 | 68 | 101 | 101 |

HEADER_VALID minus PREAMBLE_DETECTED (ms):

| n | mean | stdev | min | max | p95 abs |
|---|---|---|---|---|---|
| 17 | 588.65 | 9.97 | 563 | 596 | 596 |

Estimated preamble-detection delay: **74.9 ms** after TX start (diagnostic; the SyncStamp does not depend on it).

## Node c3

Sync packets sent: 44. TX_LATE: 0.

Radio TX start minus nominal slot start (ms), 0 in every cell when the Tx start is deterministic (ADR-0016):

| n | mean | stdev | min | max | p95 abs |
|---|---|---|---|---|---|
| 44 | 4.05 | 0.48 | 3 | 7 | 4 |

| ce | n | mean | stdev | min | max | p95 abs |
|---|---|---|---|---|---|---|
| 0 | 14 | 4.14 | 0.86 | 3 | 7 | 7 |
| 1 | 16 | 4.00 | 0.00 | 4 | 4 | 4 |
| 2 | 14 | 4.00 | 0.00 | 4 | 4 | 4 |

Tx ramp, radio TX start minus Radio.Send (ms), the platform's TX_RAMP_MS:

| n | mean | stdev | min | max | p95 abs |
|---|---|---|---|---|---|
| 44 | 4.05 | 0.48 | 3 | 7 | 4 |

## Cross-node pairing

Received Sync packets matched to a sender by (ep, ph, ce): 16; unmatched: 1.

Receiver stamp minus sender radio TX start, receiver WARM (ms):

| n | mean | stdev | min | max | p95 abs |
|---|---|---|---|---|---|
| 14 | -4.36 | 1.39 | -6 | -1 | 6 |
