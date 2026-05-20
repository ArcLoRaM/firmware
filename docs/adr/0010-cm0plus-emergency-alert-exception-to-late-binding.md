# ADR-0010: CM0+ Emergency Alert — Deliberate Exception to Late-Binding Packet Assembly

## Status
Accepted

## Context
ADR-0008 established that CM4 is the sole producer of payload content. CM0+
assembles packets at TX time by combining CM4-generated payloads with MAC
variables (routing headers, hop count, destination) drawn from shared memory.
This rule holds across all packet types: sensor data, beacon, sync, diagnostic.

A specific failure mode breaks this invariant: CM4 is dead (application core
hang, crash, or hardware fault) while CM0+ and the radio remain operational.
In this state:

- CM4 cannot produce any payload — `MeshUplinkQueue` is empty and will not
  be refilled.
- The node continues to participate in TDMA slots (CM0+ is alive, IWDG is
  being kicked, radio is functional).
- From the gateway's perspective, the node has gone silent — indistinguishable
  from a total node failure, a radio failure, or a network partition.

An Arctic operator surveilling nodes kilometers apart requires human
intervention for servicing. Silent failure with no over-the-air indication
forces the operator to treat a recoverable CM4 hang identically to a destroyed
node — potentially dispatching a team unnecessarily, or worse, not dispatching
when the node is salvageable.

Two options were considered:

- **Option A — Maintain strict Late-Binding:** CM4 dead means no payload, no
  transmission. Node goes silent until CM4 resets and recovers. The operator
  receives no over-the-air signal.

- **Option B — CM0+ Emergency Alert exception:** CM0+ produces a minimal
  synthetic payload when it detects CM4 heartbeat failure, writes it to a
  dedicated `EmergencyAlertSlot` in shared memory, transmits it at the next
  available `Mesh_Uplink` TX slot, then resets CM4. The payload carries only:
  node ID, `DIAG_ALERT` type, and the RTC timestamp of failure detection.

## Decision
Option B. CM0+ is permitted to produce one payload type: `DIAG_ALERT`. This
is the sole exception to the Late-Binding rule. All other payload content
originates exclusively from CM4.

The implementation path keeps the exception contained:

1. A single-entry `EmergencyAlertSlot` structure in shared memory — separate
   from `MeshUplinkQueue`, which CM4 owns. CM0+ writes to it only on heartbeat
   failure detection.
2. At every `Mesh_Uplink` TX opportunity, CM0+ checks `EmergencyAlertSlot`
   before `MeshUplinkQueue`. If populated, it assembles and transmits the
   alert at `HIGH` priority.
3. CM4 reset via RCC follows after the alert is queued (existing mechanism).
4. `EmergencyAlertSlot` is cleared on CM4 restart.

CM0+ still applies the standard Late-Binding assembly step to the alert
(routing headers from Routing State) — only the payload content itself
originates from CM0+.

## Consequences
The Late-Binding rule now has one documented exception. Any future reader
seeing CM0+ write to `EmergencyAlertSlot` must not treat it as a bug. The
`DIAG_ALERT` DiagType value (`0x04`) is reserved exclusively for this path;
CM4 may never produce a `DIAG_ALERT` payload.

The `EmergencyAlertSlot` is a new shared memory structure with CM0+ as writer
and CM0+ as reader — CM4 never touches it except to clear it on restart. Its
placement must be in the shared SRAM2 region visible to both cores.

The alert payload is intentionally minimal. It does not attempt to capture
diagnostic metrics (battery, RSSI, etc.) — CM4 state is unknown at the time
CM0+ produces it and cannot be safely read. The sole purpose is to give the
operator a recoverable-hang signal versus silent total failure.
