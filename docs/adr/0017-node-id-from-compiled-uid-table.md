# One-byte Node ID from a compiled UID table

A node's identity is one byte, the Node ID, because it is carried in packets (Beacon, Diagnostic, cluster payloads) where every byte costs airtime.
Each board is registered once in a table compiled into every build (`Common/Protocol/node_id.c`), mapping the MCU 96-bit unique ID (UID) to its Node ID.
At boot a board looks up its own UID; one build per node class serves every board.
Values 1-254 are assignable, `0x00` means unprovisioned (UID not in the table) and `0xFF` is reserved for broadcast.

The CM0+ `BOOT` line logs the Node ID and the full UID (`id=2 uid=002000415642500a20383354`).
The UID costs nothing there (one line per boot) and ties every dataset to a physical board, whatever serial port or node class it had (roles and ports were swapped during the 2026-09-26 bench, issue #42).
A new board boots with `id=0`; `arclog` flags that line with the exact table entry to add, and the board works after the next build.

## Considered Options

**Hash the UID to one byte (CRC-8 or FNV-1a).**
No registration at all, but 256 values give a birthday collision probability of ~2 % with 4 boards, ~16 % with 10 and ~50 % with 20, and a duplicate Node ID silently corrupts routing and diagnostics.
Rejected: registering two or three bench boards once is no burden, and a table guarantees uniqueness (host-tested) where a hash can only detect clashes after the fact.

**Provision the Node ID in internal flash or OTP.**
The right answer for large deployments, and where `CONTEXT.md` already places provisioning constants.
Deferred: it needs a flash layout, a provisioning tool and a procedure, a CubeIDE full-chip erase wipes internal flash, and OTP is write-once.

**Gateway-assigned Node ID at join (the node sends its UID once, C3 answers with a short ID).**
Deferred to the network-join design: it needs a join exchange and persistent state on C3.

## Consequences

- **Adding a board is a code change and a rebuild of every board.** Acceptable at bench scale (two or three boards); the table is replaced by provisioning or join-time assignment before large deployments.
- **`NodeId_Self()` / `NodeId_FromUid()` are the only entry points.** Replacing the table by flash, OTP or join-time assignment changes their body, not their callers.
- **Both cores read the UID themselves**, so the Node ID needs no shared-memory field.
- **Uniqueness is a host test** (`Tests/unit/test_node_id.c`): duplicate Node IDs, duplicate UIDs and the reserved values fail the build of the tests, not a field run.
- **An unprovisioned board still runs.** No packet carries the Node ID yet; once one does, a board with `id=0` must not transmit it.
