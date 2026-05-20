# ADR-0010: DirectionMode Split and CellEligibilityMask for Dynamic Phases

## Status
Accepted

## Context
The `cell_bitmap[3]` field in the Phase struct encodes a fixed Tx/Rx direction per
Node Class per Slot, applied uniformly across all Cells in a Phase. This works for
phases where direction is determined purely by Node Class (`Mesh_Beacon`,
`Mesh_Downlink`, `Sync`).

Two phases require per-cell direction that varies at runtime:

- **`Mesh_Uplink`**: a node's role in each Cell depends on `cell_index % 3` vs
  `hop_count % 3` (uplink-transmit, relay-receive, or sleep). Different nodes of
  the same class have different roles in the same Cell. The static bitmap cannot
  express this.
- **`Cluster_Exchange`**: eligibility depends on the Cell Permit decoded from the
  phase header slot. The static bitmap cannot express this either.

Two remediation options were evaluated:

- **Option A — Wake every cell, MAC decides**: TDMA Machine wakes for every Cell
  in a dynamic phase; MAC State Machine determines direction and participation at
  each wake. Simple table, but C2 wakes for cells it then sleeps through. The
  efficiency loss is bounded (2 of 3 wasted wakes) for `Mesh_Uplink` but is
  unacceptable for `Cluster_Exchange`, where many cells may be inactive for a
  given node, making the wake overhead dominant.
- **Option B — MAC pre-computes eligibility**: MAC State Machine writes a
  per-cell eligibility signal to shared memory after each topology update. TDMA
  Machine reads it at each Cell boundary and skips ineligible Cells without
  waking. Adds one shared memory read per Cell boundary but eliminates all
  wasted wakes.

## Decision
Option B, with a `DirectionMode` flag per Phase (Option X):

1. Add `direction_mode` (`DIRECTION_STATIC` / `DIRECTION_DYNAMIC`) to the Phase
   struct. `DIRECTION_STATIC` phases use `cell_bitmap[3]` as before. In
   `DIRECTION_DYNAMIC` phases `cell_bitmap[3]` is present in the struct but
   ignored; the TDMA Machine reads `CellEligibilityMask` from shared memory
   instead.

2. Define `CellEligibilityMask`: a 3-bit shared memory value written by the MAC
   State Machine. Bit N = 1 means wake for Cells where `cell_index % 3 == N`.
   For `Mesh_Uplink`, MAC computes it from `hop_count` after each beacon:
   `mask = (1 << (hop_count % 3)) | (1 << ((hop_count + 1) % 3))`. Default
   value before first beacon: `0x00` (skip all — safe because a node without a
   valid `hop_count` must not transmit). For `Cluster_Exchange`, the Cell Permit
   serves the equivalent role.

3. At each Cell boundary in a `DIRECTION_DYNAMIC` phase, the TDMA Machine checks
   `(cell_eligibility_mask >> (cell_index % 3)) & 1`. If clear, it advances the
   cursor to the next eligible Cell and reprograms the alarm — no wake occurs.

4. At wake in a `DIRECTION_DYNAMIC` phase, the MAC State Machine determines Tx vs
   Rx direction from `cell_index % 3` vs `hop_count % 3`. No additional shared
   memory is needed for direction — only eligibility is pre-computed.

Phase classification:
| Phase | `direction_mode` |
|---|---|
| `Mesh_Beacon` | `DIRECTION_STATIC` |
| `Mesh_Uplink` | `DIRECTION_DYNAMIC` |
| `Mesh_Downlink` | `DIRECTION_STATIC` |
| `Cluster_Exchange` | `DIRECTION_DYNAMIC` |
| `Sync` | `DIRECTION_STATIC` |

## Consequences
The TDMA Machine gains one shared memory read per Cell boundary in dynamic phases.
The `cell_bitmap[3]` field is retained in the Phase struct for all phases to keep a
uniform layout; it is unused when `direction_mode == DIRECTION_DYNAMIC`. The MAC
State Machine gains responsibility for writing `CellEligibilityMask` after every
beacon update. The hop-count mod-3 formula remains exclusively in the MAC State
Machine (see ADR-0002) — the TDMA Machine consumes only the pre-computed mask and
has no knowledge of the formula.
