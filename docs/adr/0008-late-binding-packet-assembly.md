# ADR-0008: Late-Binding Packet Assembly — CM0+ Assembles at TX Time

## Status
Accepted

## Context
At TX time, CM0+ needs a complete on-air packet: payload + routing headers
(destination peer, route cost context, etc.). Two assembly models were considered:

- **Early binding**: CM4 pre-assembles complete packets — payload + destination —
  and places them in the TX queue. CM0+ transmits them verbatim.
- **Late binding**: CM4's TX queues hold payloads only. CM0+ assembles the
  complete packet at TX slot boundary by combining the payload with routing
  headers drawn from CM4's current Routing State.

Early binding fails under dynamic topology: any peer becoming unreachable
between enqueue and TX invalidates the embedded destination. Correcting this
requires CM4 to reassemble every queued packet on each topology change — an
O(queue_depth) operation triggered by unpredictable external events, and
impractical at scale.

## Decision
Late binding. CM0+ assembles packets at TX time. CM4's TX queues hold payloads
only. CM0+ reads the Routing State at the moment of assembly to determine the
current best peer. The routing decision is always fresh — topology changes are
absorbed with zero queue management overhead.

## Consequences
CM0+ must read the Routing State on every TX assembly — a shared memory read at
slot boundary, negligible cost. The Routing State becomes a critical shared
structure: CM4 must keep it consistent, and CM0+ must treat it as read-only.
CM4 can no longer embed destination assumptions in queued payloads. The TX queue
depth reflects payload backlog, not routing intent — overflow policy and priority
tiers govern payload scheduling, not destination affinity.
