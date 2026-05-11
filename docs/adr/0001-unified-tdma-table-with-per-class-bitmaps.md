# ADR-0001: Unified TDMA Table with Per-Node-Class Cell Bitmaps

## Status
Accepted

## Context
C1, C2, and C3 nodes have fundamentally different radio behavior within the same
Phase. When C2 transmits, C1 may listen or sleep — this cannot be captured by a
simple eligibility mask. Two options were considered:

- **Option A**: One separate TDMA Table per Node Class (3 tables). Tables must
  have matching Phase boundaries — this must be verified externally, which is
  error-prone and a maintenance burden.
- **Option B**: One unified TDMA Table where each Phase carries a `cell_bitmap[3]`
  array indexed by Node Class, and a `participant_mask` indicating which classes
  are active at all.

## Decision
Option B. The unified table makes Phase boundary consistency structurally
guaranteed — all Node Classes share `slot_count`, `cell_count`, `slot_active_ms`,
and `gap_after_ms`. Per-class behavior (Tx/Rx) is readable side-by-side in one
struct. The Frame Cursor is a single pointer into one table.

## Consequences
Each Phase struct is slightly larger (3× the Cell bitmap). Node Class identity
must be known to the Protocol State Machine at boot. Phases where a class has no
activity are handled via `participant_mask` — non-participants skip the Phase
entirely without examining the bitmap.
