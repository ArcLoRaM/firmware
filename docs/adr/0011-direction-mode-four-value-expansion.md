# ADR-0011 — DirectionMode Three-Value Finalisation

## Status
Accepted — 2026-05-22 (revised from initial draft 2026-05-21)

## Context

ADR-0010 introduced a two-value `DirectionMode` enum (`DIRECTION_STATIC` /
`DIRECTION_DYNAMIC`) to distinguish phases where per-cell Tx/Rx direction is
fixed in the TDMA Table from phases where the MAC writes an eligibility mask at
runtime. That split was designed around `Mesh_Uplink` (MAC-written
`CellEligibilityMask`, ineligible cells slept).

Three phases expose behaviours that neither original value can represent:

**`Mesh_Beacon`** — every node must be able to receive beacons from peers at
any hop depth, including peers at the **same hop count**. A deterministic mod-3
Tx assignment (e.g., `cell_index % 3 == hop_count % 3`) fails here: two nodes
at the same hop count would always transmit in the same cells and never hear each
other, making same-depth route discovery impossible. Additionally, `DIRECTION_DYNAMIC`
sleeps ineligible cells — but every non-TX cell in `Mesh_Beacon` is a receive
opportunity and must not be slept.

**`Mesh_Downlink`** — downlink packets flow C3 → C2 in the reverse direction
of `Mesh_Uplink`. The relay chain can be staggered using the same mod-3 mechanism
but with a mirror formula (sleep at uplink-Tx residue rather than uplink-sleep
residue). This fits `DIRECTION_CELL_SKIP` exactly — ineligible cells slept,
MAC decides relay-Tx vs relay-Rx at each wakeup.

**`Sync`** — C2 nodes alternate between participation cycles (CT retransmission,
Tx every cell) and audit cycles (Rx-only). The decision is whole-phase and
uniform — a single bit at phase entry, not a per-cell mask.

The core asymmetry driving three distinct values:
- `Mesh_Uplink` / `Cluster_Exchange` / `Mesh_Downlink`: **sleep** ineligible cells;
  eligibility from `CellEligibilityMask` (hop-count residue formula, direction-dependent)
- `Mesh_Beacon`: **Rx** in all non-TX cells — no sleeping; TX cells selected randomly
  at phase entry from MAC-internal state — no shared memory bitmap
- `Sync`: **one direction for the entire phase** — no per-cell variation

`DIRECTION_STATIC` — a fully fixed compile-time bitmap — has no phase that
requires it: every phase has at least one class whose direction is determined
at runtime. It is removed rather than kept dormant.

## Decision

Replace the two-value enum with three values:

```c
typedef enum {
    DIRECTION_CELL_SKIP,   // CellEligibilityMask (3-bit); ineligible cells slept — Mesh_Uplink, Cluster_Exchange, Mesh_Downlink
    DIRECTION_MAC_CELL,    // MAC decides Tx/Rx per-cell from internal state; every cell woken — Mesh_Beacon
    DIRECTION_MAC_PHASE,   // phase_tx_flag (uint8_t); whole-phase Tx or Rx — Sync
} DirectionMode;
```

**`Mesh_Beacon` uses `DIRECTION_MAC_CELL`:** the TDMA Machine wakes the node for
every cell and passes the opportunity to the MAC. At phase entry, the MAC
randomly selects K cell indices (K is a provisioned constant) in which to
transmit the node's `BeaconPayload`. All other cells are receive. No bitmap is
written to shared memory — the selection is MAC-internal state. `BeaconTxBudget`
gates whether the selected Tx cells are actually used (see CONTEXT.md).

The random K-of-N selection solves the same-hop-count blindness: with mod-3,
two nodes at the same depth always transmit simultaneously and never hear each
other. With random selection, the probability of simultaneous overlap for both
K TX cells is `(K/N)²` — bounded and decreasing with N.

**`Mesh_Downlink` uses `DIRECTION_CELL_SKIP`** with a mirror `CellEligibilityMask`
formula. The downlink relay chain staggering is:

```c
// downlink: sleep at uplink-Tx residue
uint8_t dl_relay_residue   = (hop_count + 2) % 3;   // Tx DATA downward
uint8_t dl_receive_residue = (hop_count + 1) % 3;   // Rx DATA from upstream
cell_eligibility_mask_dl   = (1 << dl_relay_residue) | (1 << dl_receive_residue);
```

This produces a closed relay chain (each node's dl-receive residue matches its
upstream's dl-relay residue) and ensures a node's downlink-sleep residue equals
its uplink-Tx residue — uplink and downlink traffic never share active cells.

**`phase_tx_flag`** (`uint8_t`) — written by the MAC before each `Sync` phase.
`1` = Tx every cell (participation cycle); `0` = Rx every cell (audit cycle).
Read once at phase entry by the TDMA Machine.

`DIRECTION_STATIC` and `static_cell_bitmap[3]` are removed from the Phase struct.
`DIRECTION_DYNAMIC` is renamed `DIRECTION_CELL_SKIP` to make sleep semantics
explicit in the name. `DIRECTION_MAC_BITMAP` (initial draft name) is replaced by
`DIRECTION_MAC_CELL` to reflect that no bitmap is written to shared memory.

## Alternatives Considered

**Keep `DIRECTION_STATIC` as reserved** — retain the value with no current
assignment, for possible future phases with fixed bitmaps. Rejected: dormant enum
values mislead readers into searching for phases that use them. Re-adding the
value if a genuinely static phase appears later is trivial.

**Single `DIRECTION_MAC_OWNED` value** — one value for all MAC-driven phases.
Rejected: the sleep/no-sleep distinction between `Mesh_Uplink` and `Mesh_Beacon`
would become an implicit MAC convention rather than a struct-level invariant,
making it easy to misimplement.

**Deterministic mod-3 Tx assignment for `Mesh_Beacon`** — assign TX cells using
`cell_index % 3 == hop_count % 3` (same as `Mesh_Uplink`). Rejected: nodes at
the same hop count always transmit in the same cells, making same-depth peer
discovery impossible. Route redundancy and initial topology formation both depend
on discovering same-depth peers.

**`Mesh_Downlink` under `DIRECTION_MAC_CELL` (no-sleep, random Tx)** — treat
downlink relay similarly to beacon flooding. Rejected: downlink relay is
deterministic and directional; every C2 that receives a downlink packet relays it
exactly once. Random flooding would require de-duplication at every hop and
inflates airtime without benefit. The mod-3 mirror pattern staggers relays
efficiently with zero redundant transmissions.

**Merge `DIRECTION_MAC_CELL` and `DIRECTION_MAC_PHASE`** — Sync could select
K=`cell_count` cells (all-Tx) or K=0 (all-Rx). Rejected: Sync's whole-phase
uniformity is a protocol invariant. Encoding it as a separate mode prevents
accidental partial-Tx Sync provisioning, which would be a silent CT violation.
The TDMA Machine also benefits: it reads `phase_tx_flag` once at phase entry
rather than iterating a cell list.

## Consequences

- `DirectionMode` enum changes from 2 to 3 values. TDMA Machine requires three
  dispatch branches at cell boundary evaluation.
- `CellTxMask` shared memory value is not introduced. `Mesh_Beacon` Tx scheduling
  is MAC-internal only.
- `CellEligibilityMask` now covers three phases (`Mesh_Uplink`, `Cluster_Exchange`,
  `Mesh_Downlink`). MAC must write the appropriate formula before each phase.
- `phase_tx_flag` is the sole new shared memory write introduced by this ADR.
- `static_cell_bitmap[3]` field removed from `Phase` struct.
- Former `DIRECTION_DYNAMIC` references in code must be renamed to
  `DIRECTION_CELL_SKIP`. `DIRECTION_MAC_BITMAP` (if it appeared in any draft code)
  must be renamed to `DIRECTION_MAC_CELL`.
