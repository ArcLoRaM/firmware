# ADR-0004: Cluster_Exchange Footer Is a Contention Slot, Not a Scheduled Slot

## Status
Accepted

## Context
The Cluster_Exchange Phase ends with an open slot where nodes may transmit either
a data packet or a join request. Both require an ACK from the cluster master but
have different packet sizes. The MAC strategy involves channel sensing and random
backoff — inherently probabilistic and variable in packet type. Encoding this
explicitly in the TDMA Table would require the table to model variable packet
types, conditional Tx/Rx behavior, and backoff logic — all of which belong in
the Protocol State Machine.

## Decision
The Phase Footer Slot for Cluster_Exchange has `kind = CONTENTION`. Its
`duration_ms` is set to max(data+ACK ToA, join_request+ACK ToA) — the
worst-case exchange duration. The TDMA Table encodes only this duration. All
contention logic — channel sensing, random backoff, packet type selection (data
vs join request), and ACK handling — is implemented entirely in the Protocol
State Machine.

## Consequences
The TDMA Table cannot predict exact traffic in the footer slot. Observability
of footer activity requires Protocol State Machine instrumentation, not table
inspection. Any future change to the contention MAC strategy (different backoff
algorithm, additional packet types) requires no change to the TDMA Table format.
