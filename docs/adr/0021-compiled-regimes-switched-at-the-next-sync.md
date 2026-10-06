# ADR-0021: Compiled Regimes, switched by the C3 at the next Sync phase

## Status
Accepted. Supersedes in part ADR-0003 (one TDMA Table) and the rejected run-time switch of ADR-0020.

## Context
One schedule cannot serve every stage of a network's life.
Installing nodes in the field needs fast locking and a visible link quality; an installed network needs the least energy for its load; collecting a backlog needs throughput.
ADR-0003 compiled one TDMA Table behind a single accessor and deferred flash-resident and radio-negotiated tables.
ADR-0020 rejected a run-time switch of the Sync schedule because a product node must not be able to change its Sync overhead.

## Decision
The network runs one **Regime** at a time (CONTEXT.md: Regime, Regime Transition, Common Sync Phase).

- **A Regime is a whole bundle, compiled into every node**: its TDMA Table, its MAC strategy and its frequency plan. The MAC strategy may differ entirely between Regimes, not only in parameters. The radio carries only a Regime index, never a table.
- **The single accessor stays** (ADR-0003); it answers for the current Regime.
- **Every Regime fits the duty-cycle budget** for every node class, checked by a host test, and exists in both Sync Profiles (`DEV`, `PROD`) so that it runs on the NUCLEO boards. The overhead of each Regime is fixed at compile time: what ADR-0020 protected still holds, only which compiled Regime runs changes.
- **The C3 is the sole authority, and the network runs one Regime.** On the C3 the CM4 owns the choice (from an operator command) and the CM0+ applies it; on C1 and C2 the CM0+ learns it from Sync and tells the CM4.
- **The Regime index travels in every SyncPayload** (amends ADR-0005), so a cold node learns the running Regime from its first Sync.
- **The switch is immediate at the next Sync phase**: the C3 puts the new index in its next Sync phase, and every node that receives that phase switches at its end. There is no countdown.
- **Common Sync Phases make it safe**: the Sync phases of the slowest Regime appear at the same instants and with the same structure (cells, slot duration, discovery channel, Sync MAC) in every Regime, one grid per Sync Profile. Every Frame starts on one. A node that missed the switch keeps the old table until the next Common Sync Phase, hears it there, and switches.
- **A Regime Transition changes nothing else**: routes, queues, duty-cycle credit and clock state carry over. The topology does not depend on the Regime.
- **A new network starts in the Deployment Regime.**

## Considered Options
- **Parameters on one fixed Frame structure.** Rejected for now: it brings back the versioning problem ADR-0003 avoided. Parameterised Regimes are a later phase.
- **Tables in flash or sent over the radio** (ADR-0003 Options B and C). Rejected: no node can disagree on a table's content when only an index is sent.
- **Local or per-cluster Regimes** (a C2 on low battery). Rejected: TDMA alignment needs one Frame for the whole network.
- **A scheduled switch with a countdown in Frames.** Rejected: the Common Sync Phases bound a missed switch to one Common Sync period without a countdown, and the switch then waits at most one Sync period.
- **Identical Sync phases in every Regime.** Rejected: the Deployment Regime could then not sync faster than the Low Power Regime.

## Consequences
- A node that missed a switch runs the old table for up to one Common Sync period and may transmit into another Regime's phase. Accepted; a guard is added only if the bench shows collisions.
- The Sync Profile becomes a second axis: every Regime is built in `DEV` and `PROD`.
- The boot burst of ADR-0020 is superseded in purpose by the Deployment Regime.
- A C3 that reboots starts in Deployment until it stores its Regime in NVM (a later phase).
