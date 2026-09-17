# ArcLoRaM Sync Propagation via Concurrent Transmission

## Purpose of this Document

> **Status: deferred.** The pure CT model described here is deferred. The
> current implementation uses a three-tier relay model with partial CT
> (same-hop-count concurrent transmission). See `CT Sync Propagation Model`
> in `CONTEXT.md` for the current design. This document is retained as the
> design reference for when the pure CT model is implemented.

This document captures the design decisions made for integrating **Concurrent Transmission (CT)** into ArcLoRaM's sync packet propagation mechanism. It explains the core CT principle, the rationale for each acknowledged decision, and how the proposal interacts with the existing ArcLoRaM architecture documented in `CONTEXT.md` and the project philosophy document.

The goal of CT-based sync is to **collapse the per-hop sequential sync forwarding into a single network-wide synchronised pulse**, reducing Sync Phase airtime to a constant independent of network depth while preserving the existing TDMA structure, drift correction logic, and node hierarchy.

---

## Part 1 — The Core Principle of Concurrent Transmission

### Why CT Works at All

Conventional wireless protocols treat overlapping transmissions as failures to be avoided through carrier sensing, randomised backoff, or scheduling. Concurrent Transmission inverts this premise: **multiple nodes transmit the same payload simultaneously, and the receiver decodes one of them**. The protocol gains efficiency by eliminating collision-avoidance overhead; the receiver gains reliability through redundancy without coordination.

For CT to be viable, the physical layer must support reception under synchronised packet collisions. The Liao et al. paper (*Multi-Hop LoRa Networks Enabled by Concurrent Transmission*, IEEE Access 2017) establishes that LoRa supports this through a specific combination of effects:

- **Capture effect**: when one transmitter's signal arrives at the receiver with ~3 dB more power than the others, the receiver locks onto and successfully decodes the strongest signal, treating the others as interference.
- **Frequency-domain energy spreading**: LoRa's narrow subcarrier spacing (30.5 Hz at SF12) combined with inevitable carrier frequency offset between independent oscillators causes interferer energy to spread across adjacent FFT bins, providing additional power margin for the captured packet.
- **Time-domain energy spreading**: when concurrent transmissions are offset by a fraction of a symbol time, each interferer's symbol energy spreads across two adjacent symbols at the receiver, again providing power margin. Maximum benefit occurs at 0.5·T_S offset.

The combined effect: a LoRa receiver under CT requires only ~3 dB power offset between transmitters (much less than the equivalent requirement for IEEE 802.15.4 systems), and the time- and frequency-domain spreading effects often provide this margin "for free" through natural oscillator variability.

### Why CT Is Particularly Suited to Sync Propagation

A sync packet has properties that align perfectly with CT's strengths:

- **All transmitters carry identical payload** — the Frame Epoch is canonical, sourced from the SyncAnchor's prior transmission and propagated through the previous Sync Phase. Every C2 transmitting in a given Sync slot carries bit-identical content.
- **The receiver does not need to identify the source** — any successfully decoded sync packet provides a valid timing correction. C1s and audit-cycle C2s do not care which specific upstream node's transmission they captured.
- **Latency reduction is high-value** — collapsing N sequential hops into one slot directly reduces Sync Phase airtime, which feeds into energy budget and TDMA Frame efficiency.

This is the architectural insight that makes CT particularly attractive for ArcLoRaM: the sync packet's natural structure exhibits all the properties CT requires, and the alternatives (sequential hop-by-hop forwarding) carry costs (Frame airtime, energy, latency) that scale with network depth.

---

## Part 2 — Design Decisions Registered

### Decision 1 — `sync_cell_index` Semantics

**Registered:** `sync_cell_index` identifies the TDMA slot index within the Sync Phase, identical across all concurrent transmitters in that slot. It is **not** related to mesh depth or hop count from the SyncAnchor.

**Relation to existing structure:** The `SyncPayload` struct in `CONTEXT.md` retains its current 10-byte layout. The `sync_cell_index` field's meaning is clarified rather than changed: it continues to indicate which Sync Phase slot the packet belongs to, used by the receiver to compute `expected_offset_ms` relative to the Frame Epoch. Under CT, all concurrent C2 transmitters in a given Sync slot stamp the same `sync_cell_index` because they are all transmitting in the same TDMA slot.

**Implication:** No payload restructure required. The existing sync algorithm and `expected_offset_ms` computation remain valid.

### Decision 2 — Drift Budget and Modulation Choice

**Registered:** SF12/BW125 is the confirmed sync modulation. The target accumulated drift budget between any two concurrent transmitters is **16 ms (≈ 0.5·T_S)**, treated as the operating sweet spot. Integer multiples of T_S — particularly 1·T_S (~32.7 ms) — must be avoided, as these are the destructive offsets where PRR collapses according to Figures 6 and 10 of the Liao paper.

**Relation to existing structure:** This decision directly constrains the **beacon cadence** mentioned in the existing sync mechanism. The hardware experiments referenced in section 2.5 of the philosophy document (`acceptable drift thresholds for low-cost oscillators`, `optimal synchronisation interval`) now have a concrete target: keep inter-node drift below 16 ms between consecutive Sync Phases. With a 20 ppm TCXO this corresponds to ~800 seconds; with cheaper crystals at Arctic temperature extremes (~50 ppm), it tightens to ~5 minutes.

**Implication:** The Sync Phase cadence in the TDMA Frame must be sized to maintain inter-C2 relative drift below 16 ms under worst-case temperature conditions. This is now a measurable, falsifiable design constraint.

### Decision 3 — Relevant CT Mechanisms

**Registered:** The three CT-LoRa mechanisms most relevant to ArcLoRaM's sync propagation are:

1. **Capture effect at ~3 dB power offset** — the primary survival mechanism for LoRa under concurrent transmission, requiring less power separation than other standards.
2. **Time-domain energy spreading at 0.5·T_S offset** — the engineered benefit that provides additional power margin, exploitable via deliberate sub-symbol timing jitter.
3. **Preamble-locking ceiling at ~3·T_S (~100 ms at SF12/BW125)** — the hard failure mode where receivers lock onto a weak leading packet's preamble and fail SFD detection or payload demodulation when the stronger packet arrives.

**Relation to existing structure:** The preamble-locking ceiling defines the **network out-of-sync condition** for ArcLoRaM. If accumulated drift between any two concurrent C2 transmitters exceeds 3·T_S, CT sync fails network-wide and the system regresses to `CLOCK_COLD` across affected branches. The existing `ClockState` machine in `CONTEXT.md` already models this regression via the `Synchronized → Scanning` transition; the CT-specific threshold simply makes the trigger condition explicit.

The 16 ms operating target sits comfortably below the 100 ms ceiling, providing ~6× margin against catastrophic de-synchronisation.

### Decision 4 — Mitigating the Equidistant-C1 Failure Mode

**Registered:** The failure mode where a C1 sits roughly equidistant between two C2s (producing near-0 dB power offset at the C1 and breaking capture effect) is mitigated by a layered strategy:

1. **Primary — variable TX power across C2 nodes**, producing power offset differentiation at receivers without requiring receiver-side changes. The specific scheme (randomised per cycle, hop-count-based, deterministic per node) is an implementation choice subject to empirical validation.
2. **Secondary — multiple Sync slots per Sync Phase**, already supported by the existing TDMA structure (N identical Cells × 1 Slot per Cell), providing statistical redundancy across independent oscillator-phase realisations.
3. **Tertiary — offset-CT-style timing jitter** within [0, 0.5·T_S], added only if the primary and secondary mitigations prove insufficient under empirical testing.

**Relation to existing structure:** The "secondary" mitigation costs nothing — the Sync Phase already consists of N identical Cells (per Decision 7 below), so multiple sync packets per Phase is the default. The "primary" mitigation requires a per-C2 TX power policy, which is a new concept not currently in `CONTEXT.md`. The "tertiary" mitigation, if needed, would require sub-symbol delay logic in the CM0+ TDMA Machine but would not change the TDMA Table structure.

**Implication:** ArcLoRaM's sparse topology (C2s deployed at kilometre-scale separation) makes the equidistant-C1 case the principal CT failure scenario, since dense-cluster collisions do not arise. Variable TX power is the most leveraged single mitigation.

### Decision 5 — Preamble Length

**Registered:** Preamble length is a critical CT-LoRa parameter affecting both detection probability and the preamble-locking failure window. The reference value is **LoRa's standard 8-symbol preamble at SF12**, consistent with default SX1272/STM32WL configuration and the Liao paper's experimental setup. Empirical hardware testing is required to confirm the right value for ArcLoRaM's deployment.

**Relation to existing structure:** Preamble length is not currently specified in `CONTEXT.md`. This decision adds a configuration parameter (default 8 symbols) and a validation requirement to the hardware experiment plan referenced in the philosophy document.

### Decision 6 — PreambleStamp Model Preserved

**Registered:** The existing PreambleStamp / `elapsed_ms` / `expected_offset_ms` mechanism in the sync algorithm is preserved under CT. The receiver captures the preamble arrival time in the DIO1 ISR regardless of which concurrent transmitter's signal it captured, because all participating transmitters are themselves synchronised within the 16 ms tolerance and therefore produce timing-equivalent preambles.

**Relation to existing structure:** No change to the sync algorithm described in `CONTEXT.md`. The three-packet acquisition sequence (`CLOCK_COLD → CLOCK_ACQUIRING → CLOCK_WARM`), the DIO1 GPIO ISR capture, and the `SYNC_LOCK_THRESHOLD_MS` validation all remain as documented.

**Implication:** CT integration is a transmission-side change, not a receiver-side change. The CM0+ MAC State Machine's sync RX path is unaffected.

### Decision 7 — Sync Phase Structure Preserved

**Registered:** The Sync Phase structure is unchanged. It remains a sequence of identical Cells, each containing exactly one Slot. CT changes *who transmits in each Slot* (every eligible C2 plus the SyncAnchor concurrently, rather than sequential per-hop forwarding) but does not change the Phase blueprint.

**Relation to existing structure:** The `Phase` struct in `CONTEXT.md`, the `participant_mask`, the `slot_active_ms`, and the `cell_count` fields all remain valid for the Sync Phase. The `participant_mask` for the Sync Phase expresses "C2 + C3 transmit, C1 receives" for every Cell.

**Implication:** No TDMA Table restructure required for Sync Phase. The deferred "TDMA Table class refactoring" work item in `CONTEXT.md` does not need to absorb a Sync Phase redesign.

### Decision 8 — Duty Cycle Regulatory Interpretation

**Registered:** Regulatory duty cycle interpretation is acknowledged as a potential bottleneck. Per-node duty cycle is unchanged under CT (each C2 transmits one sync packet per Sync Phase, as before), but aggregate channel occupancy in a given geographic region scales with the number of concurrent transmitters. Whether this matters depends on whether the applicable regulator (EU868 rules if Greenland follows ETSI, or Greenland's specific framework) interprets duty cycle per-device or per-channel-region.

**Relation to existing structure:** This is an open external dependency rather than an architectural decision. No change to `CONTEXT.md` is required, but the deployment plan must include a regulatory clarification step before field rollout.

### Decision 9a — C1 Sync Forwarding Behaviour

**Registered:** C1 nodes never retransmit sync packets. Only C2 and C3 (SyncAnchor) participate as concurrent transmitters in the CT sync flood. C1s are receive-only with respect to sync.

**Relation to existing structure:** This is consistent with the existing node class hierarchy in `CONTEXT.md`, where C1's role is described as "cluster-only" and "sensor data producer" with "no routing responsibilities". Sync forwarding falls into the broader category of routing-equivalent network functions, which C1 does not participate in.

**Implication:** The Sync Phase `participant_mask` excludes C1 from transmission (C1 bit clear in `participant_mask`, indicating RX-only for every Slot). C1 listens during the Sync Phase to capture sync corrections but never transmits.

### Decision 9b / 10 — All Depths Transmit Concurrently

**Registered:** All participating C2s plus the SyncAnchor transmit the sync packet concurrently in the same Sync Slot, regardless of mesh depth. The Sync Phase duration is independent of network diameter. Drift between concurrent transmitters is bounded by the inter-sync interval and the 16 ms target.

**Relation to existing structure:** This is the **principal architectural shift** introduced by CT integration. The previous model in `CONTEXT.md` (`Synchronisation originates from C3 gateways and propagates through the mesh, then through clusters`) described sequential hop-by-hop forwarding. The CT model replaces this with a single synchronised network-wide pulse.

The mechanism by which this works:

- At the end of one Sync Phase, every C2 has corrected its local clock from the sync packet it received during that Phase.
- During the next Frame, all C2s independently maintain their local clocks via their RTCs, drifting by some amount bounded by their oscillator quality and the Frame duration.
- When the next Sync Phase fires, every C2 transmits its locally-stored Frame Epoch in the same TDMA Slot, from its local clock. Because all C2 clocks were aligned at the end of the previous Sync Phase (within the inter-node drift accumulated during the Frame), their transmissions are concurrent within the 16 ms tolerance.
- Receivers (C1s, and C2s on audit cycles) capture whichever transmission has capture-effect dominance at their location and apply the standard sync correction.

**Implication:** Sync Phase airtime becomes `1 × packet_length` regardless of network depth, instead of `network_depth × packet_length`. For a 5-hop network, this is a 5× reduction in Sync Phase airtime per Frame.

### Decision 11 — Listen-to-Transmit Ratio for Audit Cycles (Deferred)

**Deferred:** This decision is part of the pure CT model, which is deferred.
The current implementation uses the three-tier per-occurrence dispatch (see
`CT Sync Propagation Model` in `CONTEXT.md`) instead of audit/participate
cycle alternation. The audit cycle concept below is retained for reference
and may be revisited when the pure CT model is implemented.

**Registered:** C2 nodes alternate between participation cycles (CT retransmission of the sync packet) and audit cycles (RX-only, no retransmission, used to measure local drift against the upstream-originated sync without self-interference). The ratio of audit cycles to participation cycles is an empirical parameter, dependent on sync packet cadence and observed drift behaviour. **Starting value: 0.5 (alternating, one audit per participation cycle).** The ratio will be tuned later based on field measurements.

**Relation to existing structure:** This introduces a new mode to the C2's MAC State Machine. The current `Paired` state for C2 implies full TDMA schedule participation including sync retransmission. Audit cycles add a "skip sync TX this Phase" variant.

The simplest implementation: the C2 maintains a local counter of completed Sync Phases. On counter values where `counter % audit_interval == 0`, the C2 sets its Sync Phase TX participation to disabled for that Phase, listens normally, captures the sync packet, computes its own drift, and re-enables TX for the next Phase. No change to the TDMA Table or the Sync Phase blueprint is required — this is purely a runtime decision in the MAC State Machine.

**Implication:** Audit cycles are necessary because a C2 that always retransmits cannot distinguish "my clock is correct" from "my clock has drifted but I'm hearing my own transmission echo back at the same wrong time". Only RX-only measurements against an upstream-originated sync detect drift accurately.

---

## Part 3 — Summary of Changes to ArcLoRaM Architecture

| Component | Change Type | Description |
|---|---|---|
| `SyncPayload` struct | None | Layout preserved; `sync_cell_index` semantics clarified (TDMA slot index, not hop depth) |
| Sync Phase blueprint | None | Phase remains N identical Cells × 1 Slot |
| `participant_mask` for Sync | Clarified | C2 + C3 transmit, C1 RX-only, for every Sync Slot |
| Sync algorithm (RX path) | None | PreambleStamp, `expected_offset_ms`, three-packet acquisition preserved |
| `ClockState` machine | None | `CLOCK_COLD/ACQUIRING/WARM` transitions preserved |
| Sync Phase airtime | Reduced (deferred) | From `network_depth × packet_length` to `1 × packet_length` — under pure CT model only |
| Sync propagation model | Replaced | Sequential hop-by-hop forwarding → concurrent network-wide pulse |
| C2 TX power policy | New | Variable TX power across C2s for equidistant-C1 mitigation |
| C2 MAC State Machine | New mode | Audit cycles (RX-only sync) interleaved with participation cycles — **deferred** (part of pure CT model) |
| Preamble length | Specified | 8 symbols (LoRa default), pending empirical confirmation |
| Beacon cadence constraint | New | Sized to keep inter-C2 drift below 16 ms under worst-case temperature |
| Regulatory compliance | Open | Duty cycle interpretation (per-device vs per-region) requires clarification |

---

## Part 4 — Open Items Requiring Empirical Validation

The following parameters and behaviours are decided in principle but require hardware experiments to finalise:

1. **Preamble length** — confirm 8 symbols is appropriate, or determine a better value.
2. **Sync Phase cadence** — measure worst-case inter-C2 drift under Arctic temperature swings and size the Frame's Sync Phase interval to stay below 16 ms.
3. **Variable TX power scheme** — choose between randomised, hop-count-based, or deterministic-per-node policies based on field testing of the equidistant-C1 scenario.
4. **Audit cycle ratio** — deferred (part of pure CT model); start at 0.5 when implemented, tune based on observed drift accumulation and sync acquisition reliability.
5. **Need for offset-CT-style timing jitter** — evaluate whether variable TX power plus multiple Sync slots per Phase is sufficient, or whether sub-symbol jitter is also required.
6. **Regulatory duty cycle interpretation** — clarify with the applicable regulator whether aggregate concurrent transmission triggers per-region duty cycle limits.

---

## Part 5 — Why This Design Is a Good Fit for ArcLoRaM

The CT-sync integration aligns with ArcLoRaM's foundational principles as articulated in the philosophy document:

- **Lifetime efficiency over throughput**: collapsing per-hop sync forwarding into a single concurrent pulse reduces energy expenditure per Sync Phase and shortens the radio-on window across the network, directly improving the ratio of operational duration to energy cost.
- **Distributed autonomy**: every C2 transmits sync independently from its local clock, without coordination with neighbours within a Sync Phase. The "synchronised pulse" emerges from prior synchronisation, not from active coordination.
- **Self-organising resilience**: the CT mechanism degrades gracefully. If some C2s fail to transmit (depleted battery, hardware fault, temporary unavailability), receivers still capture sync from the remaining transmitters as long as at least one is audible with sufficient power offset. There is no single point of failure in sync propagation.
- **Sustainability under scarcity**: reducing Sync Phase airtime from O(depth) to O(1) is exactly the kind of structural efficiency gain that compounds over multi-year deployments.

The design preserves every existing ArcLoRaM mechanism that matters — the node hierarchy, the TDMA Table structure, the sync algorithm, the ClockState machine, the inter-core architecture — and adds CT as a transmission-side optimisation. This is the cleanest possible integration: a significant efficiency gain without architectural disruption.
