# ADR-0009: Concurrent Transmission for Sync Packet Propagation

## Status
Accepted

## Context
Sync packets must reach every node in the mesh each Frame to correct RTC drift.
Two propagation models were considered:

- **Sequential hop-by-hop**: C3 transmits in slot 0; each C2 relay forwards in its
  assigned slot indexed by hop depth. Sync Phase airtime scales linearly with network
  depth: `network_depth × packet_length`.
- **Concurrent Transmission (CT)**: all C2 nodes and C3 transmit the same
  `SyncPayload` simultaneously in the same TDMA Slot. Receivers decode whichever
  transmission has capture-effect dominance (~3 dB power offset sufficient at LoRa
  SF12/BW125). Sync Phase airtime is `1 × packet_length` regardless of depth.

CT is viable here because all concurrent transmitters carry **bit-identical payloads**
(the Frame Epoch sourced from the previous Sync Phase) and receivers do not need to
identify the source. The LoRa capture effect, frequency-domain energy spreading
(narrow 30.5 Hz subcarriers), and time-domain spreading (at 0.5·T_S ≈ 16 ms offset)
combine to allow reliable decoding under synchronised collisions.

The operating constraint: accumulated inter-node drift between concurrent transmitters
must remain below 16 ms (0.5·T_S at SF12/BW125). The hard failure ceiling is ~100 ms
(3·T_S), where preamble-locking dominates and CT fails. 16 ms provides ~6× margin.

## Decision
CT. All eligible C2 nodes plus the C3 SyncAnchor transmit the sync packet
concurrently in the same TDMA Slot. Sequential per-hop forwarding is eliminated.

**Sync Phase structure is unchanged**: N identical Cells × 1 Slot.
`participant_mask`: C2 + C3 transmit (all concurrently in each Slot); C1 is RX-only.
`sync_slot_index` in `SyncPayload` is the TDMA Slot index — not hop depth — and is
identical across all concurrent transmitters in that Slot.

**C2 audit cycles**: C2 nodes alternate between participation (CT retransmission)
and audit (RX-only) cycles. Audit cycles let a C2 measure its local drift against
an upstream-originated sync without self-interference. Starting ratio: 1 audit per
participation cycle (0.5). MAC State Machine tracks the cycle counter; TDMA Table
is unchanged.

**Variable TX power**: C2 nodes use differentiated TX power to prevent the
equidistant-C1 failure mode (near-0 dB capture offset when a C1 sits equidistant
between two C2s). Specific policy (randomised, hop-count-based, or deterministic
per node) is deferred pending empirical validation.

The receiver-side sync algorithm (SyncStamp, `expected_offset_ms`, three-packet
acquisition, `SYNC_LOCK_THRESHOLD_MS`) is unchanged — CT is a transmission-side
change only.

## Consequences
Sync Phase airtime drops from O(network_depth) to O(1) — a 5× reduction at 5 hops.
This directly improves energy budget and TDMA Frame efficiency for multi-year
deployments.

Four parameters require empirical validation before field rollout: preamble length
(default 8 symbols), Sync Phase cadence (sized to maintain inter-C2 drift < 16 ms
under Arctic temperature extremes), variable TX power policy, and audit cycle ratio.
Regulatory duty cycle interpretation (per-device vs per-channel-region under EU868
or applicable Greenland framework) is an open external dependency.

If empirical testing shows CT is unreliable in the deployment environment, the fallback
is sequential hop-by-hop forwarding — the TDMA Table structure and receiver-side
algorithm support both models without change.
