# ADR-0012 — Guard Time Resolver with Alarm Look-Ahead

## Status
Accepted — 2026-06-25

## Context

The TDMA Machine applies a guard time to Rx slots so the receive window opens
before and closes after the nominal slot timing, absorbing RTC drift between
Sync corrections. In the initial implementation this is a single `#define`
(`GUARD_TIME_MS = 5u`) used inline in `TdmaMachine_SlotTask()`.

Two problems with the inline approach:

1. **Missing leading guard for non-Sync phases.** The alarm look-ahead
   (`next_slot_is_rx()`) only returns `true` for `DIRECTION_MAC_PHASE`. For
   `DIRECTION_CELL_SKIP` and `DIRECTION_MAC_CELL`, the Rx window duration
   includes `2 × GUARD_TIME_MS` but the alarm is programmed at the nominal
   time — the entire guard margin lands on the tail, not the head. This means
   the Rx node can miss a Tx preamble that arrives early due to peer clock
   drift.

2. **No upgrade path to variable guard.** Version 2 will compute guard time
   from estimated clock drift and sync age, potentially producing different
   values per slot. An inline constant cannot accommodate this without
   scattering drift logic throughout the TDMA Machine.

## Decision

### Guard Time Resolver module

Extract guard time provision into a dedicated module:

```c
/* guard_time_resolver.h */
uint32_t GuardTimeResolver_GetGuardMs(void);
```

Version 1 returns the compile-time constant `GUARD_TIME_MS`. The TDMA Machine
calls this function and applies the result in two places:

- **Alarm offset:** `alarm_ms -= GuardTimeResolver_GetGuardMs();` (wake early
  for predicted Rx slots)
- **Rx window duration:** `RadioSetRx(slot_active_ms + 2u * GuardTimeResolver_GetGuardMs());`

Single return value — the same `guard_ms` is used for both the leading offset
and each half of the window extension. Version 2 may split the interface into
`alarm_advance_ms` / `rx_extension_ms` if decoupling is needed.

### Extended Rx prediction

`next_slot_is_rx()` is extended to cover all three DirectionModes:

| DirectionMode        | Prediction method                                                  |
|----------------------|--------------------------------------------------------------------|
| `DIRECTION_MAC_PHASE`  | Read `MAC_GetPhaseTxFlag()` (existing)                           |
| `DIRECTION_CELL_SKIP`  | Compute from `CellEligibilityMask` (uplink or downlink mask based on `phase->type`) + `cell_index % 3` vs `hop_count % 3` |
| `DIRECTION_MAC_CELL`   | `cell_index >= BEACON_K_TX_CELLS` → Rx; otherwise conservatively return false |

The TDMA Machine owns the prediction (queries MAC via existing getters). The
resolver is a pure value function with no Rx/Tx awareness.

### No phase-boundary special case

Prediction inputs are stable across phase boundaries:

- `MAC_GetPhaseTxFlag()` is effectively a compile-time constant per node class
  (C1 = 0, C3 = 1, C2 = 0 for look-ahead purposes — C2 cell 0 is always Rx).
- `CellEligibilityMask` depends on `hop_count` (stable across phases) and
  `cell_index` (from the advanced cursor). The uplink/downlink mask is selected
  via `phase->type`, which is available from the TDMA Table.
- `BEACON_K_TX_CELLS` is a compile-time constant.

No boundary skip or fallback logic is needed.

### Tx node behaviour

The Tx node always transmits at nominal time — no guard adjustment on the Tx
side. The Rx guard must absorb bilateral drift (local + peer). In V1 this is
covered by the fixed 5ms constant being sized for worst-case bilateral drift
at the expected Sync correction cadence.

## Alternatives Considered

**Resolver owns Rx prediction** — `GuardTimeResolver_GetGuardMs(cursor, phase)`
returns 0 for Tx/Skip slots and `guard_ms` for Rx slots. Rejected: prediction
logic would duplicate DirectionMode awareness already present in the TDMA
Machine. The existing pattern (MAC exposes getters, TDMA Machine reads them)
is cleaner.

**Systematic phase-boundary skip** — skip look-ahead at every phase transition
as a safety net. Rejected: after analysis, all prediction inputs are stable
across boundaries. The skip would cost one unguarded leading edge per phase
transition with no correctness benefit.

**Keep guard time inline** — replace the constant with a function call but
don't create a separate module. Rejected: mixes drift estimation concerns
(V2) with cursor traversal mechanics, violating the TDMA Table / TDMA Machine
separation principle.

## Deferred (Version 2)

- Variable guard based on estimated clock drift and time since last Sync
  correction
- Bilateral drift model: Rx node estimates total drift (own + worst-case peer)
- Potential interface split: `alarm_advance_ms` and `rx_extension_ms` as
  separate return values
- Per-node guard asymmetry between communicating nodes