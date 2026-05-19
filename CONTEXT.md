# ArcLoRaM Domain Language

## Node Class

Three node classes exist. The class is a **compile-time constant** (enabling dead
code elimination) and a **shared memory constant** (so both CM4 and CM0+ can read
it without each maintaining their own copy). Both representations coexist.

| Class | Role |
|---|---|
| `C1` | End node. Cluster-only. Sensor data producer. Communicates solely with its cluster master. |
| `C2` | Relay. Cluster master for C1 nodes. Mesh backbone participant. May also collect sensor data. |
| `C3` | Gateway. Mesh backbone terminus. SyncAnchor — originates Sync packets, never enters `Scanning`. May act as cluster master. |

## CM4 Wake Sources

CM4 exits Stop2 on two independent hardware interrupt sources. These are
orthogonal — either can fire at any time regardless of the other.

| Source | Trigger | CM4 action |
|---|---|---|
| RTC Alarm B | Sensor acquisition schedule (programmed by CM0+ on CM4's behalf — see `AlarmBRequest`) | Run sensor acquisition cycle, write result to appropriate TX queue, write next `AlarmBRequest` to shared memory |
| IPCC interrupt (MbMux) | CM0+ fires Application Signal Channel notification | Process signal (`RX_READY`, `TX_NO_ACK`, `SYNC_LOCKED`, or `ACK_RECEIVED`), send ACK |

On receiving `SYNC_LOCKED`: CM4 writes an `AlarmBRequest` to shared memory if
not already pending, then returns to Stop2. CM0+ programs Alarm B on its next
wakeup. If a sensor cycle is already running (re-sync after sync loss), CM4 does
nothing — the existing cycle continues uninterrupted.

### AlarmBRequest
Shared memory structure written by CM4, read and consumed by CM0+. Contains the
desired next Alarm B wake time as RTC time fields (BCD, matching the RTC
register format — no conversion) and a `pending` flag (`uint8_t`, naturally atomic
on ARM Cortex-M). CM4 writes the time fields then sets `pending = 1`. CM0+
acts only when `pending == 1`, programs Alarm B, then clears `pending`. This
eliminates the race where CM0+ reads a partially-written or stale request.
CM4 writes the minimum next-alarm across all due sensors — sensor scheduling
details are deferred (see Deferred section). CM0+ is the sole writer to RTC
registers (see ADR-0007).

## TDMA Scheduling

### Frame
The top-level configurable repeating period of the TDMA schedule. Duration is a
runtime parameter (`frame_duration`). Every Frame is structurally identical — the
schedule repeats without nesting. (example: 1Hour, 24H..)

### Phase
A contiguous time subdivision of a Frame with a given Phase Type. A Frame is an
ordered array of Phases. A Phase is identified by its **index** in that array —
Phase Type is descriptive, not a unique key. The same Phase Type may appear more
than once in a Frame.

### Phase Type
Five-value enum describing the network role of a Phase. MAC-layer concept;
distinct from Radio State.

| Value | Layer | Role |
|---|---|---|
| `Mesh_Beacon` | Mesh | Link detection for routing (C2/C3 nodes) |
| `Mesh_Uplink` | Mesh | Node-to-gateway sensor data transmission |
| `Mesh_Downlink` | Mesh | C3-originated commands/config pushed down the mesh. C2 decides per-packet: relay further down the mesh, distribute to its cluster (stopping mesh propagation), or act locally. |
| `Cluster_Exchange` | Cluster | Bidirectional local communication between cluster master (C2) and end nodes (C1) |
| `Sync` | Network | Forwards a time synchronisation packet across the network. Sole purpose: propagate Frame Epoch corrections hop by hop. No data, no routing cost. Simple Cell × N, no Header/Footer Slot. |

**Mesh** phases operate at the backbone level (C2/C3, long-range, high SF).
**Cluster** phases operate at the star-subnet level (C1/C2, short-range, low SF).

### Cell
The repeating unit within a Phase. All Cells in a Phase are identical — only one
Cell blueprint is stored. A Phase is a sequence of `cell_count` identical Cells.

### Cell Blueprint
The compact Rx/Tx pattern for one Cell, stored as a `uint32_t` bitmap (1 bit per
Slot: 1=Tx, 0=Rx). Stored once per Phase. Up to 32 Slots per Cell.
Each Phase carries three Cell blueprints — one per Node Class (C1, C2, C3).

### Slot
The atomic radio access opportunity within a Cell. Each Slot corresponds to one
complete exchange: a DATA+ACK pair, or a single packet (Beacon, Sync). Guard time
is applied by the TDMA Machine — it is not a separate Slot type.

### Slot Kind
Qualifies how a Slot is accessed:
- `SCHEDULED`: direction is predetermined by the Cell blueprint (Tx or Rx).
- `CONTENTION`: any eligible node may attempt to transmit after channel sensing
  and random backoff. The MAC State Machine decides Tx vs Rx at runtime.
  Cell blueprint does not apply.

### Phase Header Slot
An optional single Slot anchored to the **start** of a Phase, outside the Cell × N
repetition. Has its own `duration_ms` and per-class bitmap. Used for management
or control frames (e.g., cluster management packet at the start of Cluster_Exchange).
Absent when `duration_ms == 0`.

### Phase Footer Slot
An optional single Slot anchored to the **end** of a Phase, outside the Cell × N
repetition. Has its own `duration_ms` and Slot Kind. In `Cluster_Exchange` this
is a CONTENTION slot: nodes sense the channel, then attempt to transmit a data
packet. The MAC State Machine handles packet type selection, sensing, and backoff
entirely. Absent when `duration_ms == 0`.

### Slot Active Duration
A per-Phase field (`slot_active_ms`) representing the worst-case duration of one
complete Slot exchange (DATA+ACK pair, or a single packet for Beacon/Sync Phases).
Actual airtime may be shorter as modulation parameters vary, but the alarm chain
always uses `slot_active_ms` to guarantee no overlap. The next alarm is programmed
as `slot_start + slot_active_ms + gap_after_ms[i]`.

### Radio State
The hardware operating state of the radio chip: `Tx | Rx | Sleep`.
Maps directly to SX127X / STM32WL PHY states. A hardware-layer concept — not to be confused with Phase Type (MAC layer).

### Guard Time
A global protocol constant (`GUARD_TIME_MS`). Not stored in the TDMA Table.
Applied by the TDMA Machine: whenever a node enters Rx mode, it opens the receive window `GUARD_TIME_MS` before the nominal Slot start and holds it until `GUARD_TIME_MS` after the nominal end. Absorbs clock drift accumulated since the last Frame Epoch correction.

### TDMA Table
The read-only schedule descriptor for one Frame. An ordered array of Phase structs.
Describes *when* and *what kind* of radio activity is scheduled. Node-agnostic:
it does not encode hop-count rules, contention logic, or sleep decisions.

```c
typedef enum { SLOT_SCHEDULED, SLOT_CONTENTION } SlotKind;

struct AnchorSlot {
    uint32_t duration_ms;    // 0 = absent
    SlotKind kind;
    uint32_t bitmap[3];      // [C1, C2, C3]; ignored if kind == CONTENTION
};

struct Phase {
    PhaseType  type;
    uint8_t    participant_mask;     // bit0=C1, bit1=C2, bit2=C3
                                     // non-participants skip Phase entirely
    uint32_t   slot_active_ms;       // worst-case duration of one Slot exchange
    uint32_t   gap_after_ms[16];     // network-wide sleep gap after slot i
    AnchorSlot header;
    AnchorSlot footer;
    uint32_t   cell_bitmap[3];       // [C1, C2, C3]: 1-bit per slot, 1=Tx 0=Rx
    uint8_t    slot_count;           // Slots per Cell (1–32)
    uint16_t   cell_count;           // Cell repetitions in this Phase
};
```

### Participant Mask
A per-Phase 3-bit field (`participant_mask`) indicating which Node Classes
participate in this Phase. Non-participant classes set no alarms within the Phase
and remain asleep for its entire duration. The TDMA Machine checks this
at Phase entry before programming any alarms.

Example: `Mesh_Uplink` has `participant_mask = 0b110` (C2 + C3 only; C1 sleeps
through the entire Phase).

### TDMA Machine
The traversal engine on CM0+. Consults the TDMA Table via a single accessor.
Never modifies it. Responsible purely for timing mechanics — when to wake and which slot is active.

**TDMA Table responsibilities** (what the table owns):
- Which Phase Types appear and in what order
- Which Node Classes participate in each Phase (`participant_mask`)
- The Rx/Tx pattern per Node Class per Slot (`cell_bitmap`)
- Slot active duration and inter-slot gaps (`slot_active_ms`, `gap_after_ms`)
- Phase Header and Footer Slot definitions
- Cell repetition count (`cell_count`)

**TDMA Machine responsibilities** (what the table does NOT own):
- Advancing the Frame Cursor and computing absolute slot times
- Checking `participant_mask` at Phase entry — if local class is excluded, skip
  Phase entirely and program alarm for next Phase start
- Programming RTC alarms for each wake-up (alarm-chain sleep model)
- Applying Guard Time on Rx Slots
- Correcting Frame Epoch on Mesh_Beacon or Sync reception

**Sleep strategy — alarm-chain model:**
Nodes are always in deep sleep between Slots. There is no idle polling or
spin-waiting. After completing Slot i, the TDMA Machine computes:

  `next_alarm = slot_start(i) + slot_active_ms + gap_after_ms[i]`

and programs the RTC before returning to sleep. On the next wake, it resumes
from the updated Frame Cursor. For long inter-Phase gaps (hours), the TDMA
Machine jumps the cursor directly to the next active Slot for the local Node
Class, using the precomputed `phase_start_offset[]` array to avoid iterating
through intermediate Phases and Cells.

**Key boundary**: TDMA Table = schedule data (read-only, node-agnostic).
TDMA Machine = timing mechanics (node-aware, alarm-driven).

### MAC State Machine
The per-opportunity decision layer on CM0+. Companion to the TDMA Machine:
where the TDMA Machine determines *when* a radio opportunity occurs, the MAC
State Machine determines *whether* that opportunity is taken and *how*.

**MAC State Machine responsibilities:**
- Maintaining the node's operational state (see states below)
- At each opportunity: deciding whether to TX, RX, or skip
- Applying hop-count mod-3 eligibility within `Mesh_Uplink` (see Hop-Count Cell Eligibility)
- Executing contention logic in CONTENTION footer Slots (CSMA + backoff)
- Selecting packet type (data vs join request) in `Cluster_Exchange` footer
- Reading the Cell Permit to decide per-cell TX eligibility in `Cluster_Exchange`
- Accepting state transition commands from CM4

CM4 can influence the MAC State Machine by writing state transition commands
to shared memory. CM0+ never waits for CM4 — it reads pre-computed state at
opportunity boundaries.

**States:**

| State | Applies to | Condition | Radio behaviour |
|---|---|---|---|
| `Scanning` | C1, C2 | No sync established (`ClockState = CLOCK_COLD` or `CLOCK_ACQUIRING`). Boot default for C1/C2. | Continuous wide RX. No TX. Listening for Sync packets only. |
| `Synchronized` | C1, C2 | Three Sync packets received, preamble offset error below threshold (`ClockState = CLOCK_WARM`). Seeking peers — discovery behaviour is implied, not a separate state. | Listens on known frame boundaries. Attempts cluster join or mesh peer exchange. No data TX. |
| `Active` | C3 | Boot default for C3. C3 is the SyncAnchor — it requires no sync acquisition. Immediately operational: transmitting Sync packets, listening for peer connections. | Follows TDMA Table (C3 slot pattern). Originates Sync packets. Accepts mesh and cluster connections. |
| `Paired` | C1, C2, C3 | C1/C2: connected to cluster master or mesh backbone. C3: at least one node (C2 or C1) is connected. | Full TDMA schedule: TX from phase-specific buffers when non-empty. |

**C3 sleep mode:** C3 does not enter Stop2. Its default operating mode between
slots is active (Run or Sleep, not deep Stop2). The TDMA Machine still programs
RTC alarms and advances the Frame Cursor identically — this is purely an LPM
configuration difference. With node class as a compile-time constant, the sleep
call at slot completion resolves to the correct mode at compile time.

**Backward transitions:**
- `Paired → Synchronized` (C1/C2): node loses cluster master or mesh partners
  but retains sync. Re-enters peer-seeking behaviour.
- `Paired → Active` (C3): all connected nodes disconnect. C3 resumes listening
  for new connections. Sync is never lost — C3 is the source.
- C1/C2 any state → `Scanning`: sync lost (RTC drift exceeds threshold, or too
  many consecutive Sync packets missed). Full three-packet acquisition restarts.
- C3 never enters `Scanning`.

### Frame Cursor
The TDMA Machine's live position within the TDMA Table.
Advances each Slot; resets to zero at Frame end.

```c
struct FrameCursor {
    uint8_t  phase_index;
    uint16_t cell_index;
    uint8_t  slot_index;
};
```

### Frame Epoch
The absolute timestamp of the current Frame's start. Set or corrected when a
`Mesh_Beacon` or `Sync` packet is received. All Frame Cursor time computations
are relative to the Frame Epoch. Nodes that miss a beacon drift until the next
one corrects it.

Stored as raw RTC fields in `RTC_BINARY_NONE` mode — BCD calendar plus raw
SubSeconds SSR (counts **down** from `PREDIV_S`). Never converted to a flat
integer; arithmetic uses the SubSeconds formula:
`ss_ms = (PREDIV_S − SubSeconds) × 1000 / (PREDIV_S + 1)`.
With `PREDIV_S = 4095` (see ADR-0009), one SSR tick = 244 µs — the finest
achievable sync correction granularity for this node.

---

## Frame Cursor Synchronisation

### SyncAnchor
The node designated to originate the sync packet in slot 0 of every `Sync` Phase.
Sole time authority for the network. All other nodes derive their wall-clock time
from the SyncAnchor transitively through relay hops.

### SyncPayload
The on-wire payload of a sync packet. Carries the `FrameEpoch` plus a
`sync_slot_index` field. Relay nodes forward the struct verbatim except for
incrementing `sync_slot_index`.

```c
typedef struct {
    uint8_t  hours;           /* BCD 00–23 */
    uint8_t  minutes;         /* BCD 00–59 */
    uint8_t  seconds;         /* BCD 00–59 */
    uint32_t subseconds;      /* Raw SSR (counts DOWN from PREDIV_S) */
    uint8_t  day;             /* BCD 01–31 */
    uint8_t  month;           /* BCD 01–12 */
    uint8_t  year;            /* BCD 00–99, years since 2000 */
    uint8_t  sync_slot_index; /* 0 = SyncAnchor, 1 = first relay, … */
} SyncPayload;                /* 11 bytes */
```

### sync_slot_index
Index of the TDMA Slot within the Sync Phase in which this packet was transmitted.
Under concurrent transmission, all participating C2 and C3 nodes transmitting in
the same Slot stamp the same `sync_slot_index` — it is not a hop-depth counter.
The receiver uses this field to compute the expected preamble arrival offset from
Frame Epoch regardless of which concurrent transmitter's signal was captured:

```
expected_offset_ms = preceding_phases_ms
                   + sync_slot_index × (sync_slot_active_ms + sync_slot_gap_ms)
```

### CT Sync Propagation Model
Sync packets are propagated via **Concurrent Transmission (CT)**: every eligible
C2 and the C3 SyncAnchor transmit the sync packet simultaneously in the same TDMA
Slot. Receivers decode whichever transmission has capture-effect dominance (~3 dB
power offset sufficient). Because all C2 clocks were aligned at the end of the
previous Sync Phase, their transmissions arrive within the 16 ms drift tolerance
(0.5·T_S at SF12/BW125) — the operating target that provides ~6× margin against
the preamble-locking failure ceiling (~100 ms, 3·T_S).

**Sync Phase structure is unchanged**: N identical Cells × 1 Slot. `participant_mask`
for Sync: C2 + C3 transmit (all concurrently), C1 RX-only. Sync Phase airtime is
`1 × packet_length` regardless of network depth.

**C2 audit cycles**: C2 nodes alternate between participation cycles (CT
retransmission) and audit cycles (RX-only, no retransmission). On an audit cycle
the C2 captures the upstream-originated sync normally and measures its local drift
without self-interference. Starting ratio: 1 audit per participation cycle (0.5).
The MAC State Machine tracks the cycle counter; no TDMA Table change is required.

**Variable TX power**: C2 nodes use differentiated TX power across participants to
provide power offset at receivers near the equidistant-failure zone. Policy
(randomised per cycle, hop-count-based, or deterministic per node) is deferred
pending empirical validation.

**Receiver-side algorithm unchanged**: PreambleStamp capture, `expected_offset_ms`
computation, three-packet acquisition, and `SYNC_LOCK_THRESHOLD_MS` validation
all remain as documented in the Sync Algorithm section.

### PreambleStamp
An in-memory RTC snapshot (`RTC_TimeTypeDef`) captured inside the DIO1 GPIO ISR
the instant a sync preamble is detected. Never persisted. Consumed immediately
by the warm-sync handler to compute `elapsed_ms`.

### ClockState
Three-value enum tracking RTC synchronisation quality.

| Value | Meaning |
|---|---|
| `CLOCK_COLD` | No Sync packet received. Boot default. MAC State Machine is in `Scanning`. |
| `CLOCK_ACQUIRING` | At least one Sync packet processed. RTC partially calibrated; sub-second precision not yet confirmed. |
| `CLOCK_WARM` | Three Sync packets received with preamble offset error below `SYNC_LOCK_THRESHOLD_MS`. Node is fully Synchronized. |

### Sync Algorithm — summary

Synchronisation requires three consecutive Sync packets to reach `CLOCK_WARM`.

**Packet 1 (`CLOCK_COLD` → `CLOCK_ACQUIRING`):** parse `SyncPayload` → call
`HAL_RTC_SetTime` / `HAL_RTC_SetDate` with BCD fields directly (no conversion) →
set Frame Cursor to start of `Sync` Phase → `ClockState = CLOCK_ACQUIRING`.
Sub-second precision is lost; cursor is approximate.

**Packet 2 (`CLOCK_ACQUIRING`, internal count = 1):** capture `PreambleStamp` in
DIO1 ISR → parse `SyncPayload` → compute sub-second deviation:
`elapsed_ms = rtc_to_ms(PreambleStamp) − rtc_to_ms(FrameEpoch)` → align RTC
sub-second register. Frame Cursor now accurate to sub-second.

**Packet 3 (`CLOCK_ACQUIRING`, internal count = 2):** capture `PreambleStamp` →
compute `clock_error_ms` from `expected_offset_ms` → if
`clock_error_ms < SYNC_LOCK_THRESHOLD_MS`: `ClockState = CLOCK_WARM`, MAC State
Machine transitions to `Synchronized`. Otherwise remain `CLOCK_ACQUIRING` and
wait for the next Sync packet.

**Sync loss:** if RTC drift exceeds threshold or too many consecutive Sync packets
are missed, `ClockState` resets to `CLOCK_COLD` and the MAC State Machine returns
to `Scanning`. The full three-packet acquisition loop must restart.

---

## Packet Assembly Model

CM4 owns payloads and routing state. CM0+ owns packet assembly.

At TX time, CM0+ pulls a payload from the appropriate phase-specific TX queue and
wraps it with routing headers drawn from CM4's current **Routing State** — the
best peer reachable at this moment. The routing decision is deferred to the last
possible moment (TX slot boundary), not made when the payload was enqueued.

This guarantees that dynamic topology changes are absorbed automatically: if a
peer becomes unreachable between enqueue and TX, CM0+ reads the updated Routing
State and targets a different peer. Pre-assembled packets with embedded
destinations would carry stale routing information and require CM4 to
reassemble the entire queue on every topology change — a dead end at scale.

### Routing State
Shared memory structure written by CM4, read by CM0+ at packet assembly time.
Contains per-peer reachability and route cost information. CM4 updates it in
response to received payloads, `TX_NO_ACK` events, and topology signals.
CM0+ consults it each time it assembles a packet for transmission.

### TX_NO_ACK Payload
When a TX slot completes without an ACK, CM0+ fires `TX_NO_ACK` via the
Application Signal Channel with a payload identifying: the destination peer that
did not respond, and the phase context. CM4 uses this to increment the peer's
failure counter, eventually declare it unreachable, and update the Routing State.
CM0+ uses it internally within the MAC State Machine for contention strategy
(backoff timing, retry decisions in CONTENTION slots).

---

## Inter-Core Signaling

### Application Signal Channel
A single MbMux `NOTIF_ACK` feature (`FEAT_INFO_APP_ID`) used exclusively by
CM0+ to signal CM4 of asynchronous events. CM0+ fires a notification; the IPCC
interrupt wakes CM4 from Stop2; CM4 processes the event and sends the ACK.

Three signal types are multiplexed on this one channel via `MsgId`:

| Signal | Fired when | Payload |
|---|---|---|
| `RX_READY` | CM0+ has written a new packet to a phase-specific RX buffer. CM4 should read and process it. | Phase type identifying which RX buffer to read. |
| `TX_NO_ACK` | A TX slot completed without receiving an ACK. | Destination peer ID + phase context. CM4 updates Routing State; CM0+ uses internally for contention strategy. |
| `SYNC_LOCKED` | Third Sync packet received with preamble offset error below `SYNC_LOCK_THRESHOLD_MS`. `ClockState` → `CLOCK_WARM`. | None — signal alone is sufficient. |
| `ACK_RECEIVED` | A TX slot completed with a successful ACK. CM4 must dequeue the delivered payload. | Queue entry identifier (e.g. sequence number assigned by CM4 at enqueue time). CM4 locates and removes the entry. |

### Application Signal Channel — Timing Invariant
The IPCC channel latch stays set until CM4 ACKs. A second `MBMUX_NotificationSnd`
before the ACK returns `-1` and silently drops the event. No re-fire mechanism is
implemented, because the scenario cannot occur:

CM0+ follows the alarm-chain model — it wakes for exactly one slot, performs one
radio action, programs the next alarm, and returns to sleep. At most one signal
fires per CM0+ wake. The minimum inter-slot gap (`gap_after_ms` minimum: 20–30 ms)
gives CM4 a window that exceeds all CM4 processing paths by an order of magnitude.
The IPCC channel is always free before CM0+ wakes for the next slot.

---

## Inter-Core Buffers

Phase-specific shared memory structures. TX Queues are written by CM4 and pulled
by CM0+ at the corresponding Phase. RX Buffers are written by CM0+ on packet
reception and read by CM4 for routing and protocol logic.

### TX Queue Priority Tiers
Three-tier priority used in all priority-bearing TX Queues. CM4 inserts packets
at the appropriate tier; CM0+ always pulls from the highest non-empty tier.

| Tier | Use |
|---|---|
| `HIGH` | Management frames, join requests, urgent alerts |
| `NORMAL` | Regular sensor data and relay payloads |
| `LOW` | Bulk or aggregated data, non-urgent telemetry |

**Overflow policy:** when a tier is full and CM4 enqueues a new payload:
- `HIGH` tier — **drop newest** (reject incoming payload). Management frames
  already queued are more important than a duplicate or competing new one.
- `NORMAL` and `LOW` tiers — **drop oldest** (evict head to make room). Fresh
  sensor data is more relevant than stale readings already waiting to be sent.
- Stalling (blocking CM4 until space opens) is never permitted — CM4 must
  return to Stop2 promptly.

### MeshUplinkQueue
TX Queue for `Mesh_Uplink` Phase. Multi-packet FIFO with three-tier priority
(`HIGH` / `NORMAL` / `LOW`). Written by CM4 (own sensor data or relayed cluster
payloads). Holds **payloads only** — routing headers are added by CM0+ at TX
time from the Routing State (see Packet Assembly Model). Pulled by CM0+ when a
Mesh_Uplink TX slot is active. C2/C3 only.

### MeshUplinkBuffer
RX Buffer for `Mesh_Uplink` Phase. Written by CM0+ on packet reception. Read by
CM4 for routing decisions (e.g. relay to cluster, consume locally). C2/C3 only.

### MeshDownlinkQueue
TX Queue for `Mesh_Downlink` Phase. Simple FIFO, no priority. Written by CM4.
Pulled by CM0+ when a Mesh_Downlink TX slot is active. C2/C3 only. Packets
carry a type field; CM4 inspects it at receive time to dispatch: apply locally
(config update), relay down the mesh (re-enqueue to `MeshDownlinkQueue`), or
distribute to cluster (enqueue to `ClusterQueue`). Detailed downlink packet
structure and dispatch rules are not yet specified.

### MeshDownlinkBuffer
RX Buffer for `Mesh_Downlink` Phase. Written by CM0+ on reception. Read by CM4
for dispatch (local application, mesh relay, or cluster distribution). C2/C3 only.

### ClusterQueue
TX Queue for `Cluster_Exchange` Phase. Multi-packet FIFO with three-tier priority
(`HIGH` / `NORMAL` / `LOW`). Written by CM4 (sensor data for C1, management
frames for cluster master). Holds **payloads only** — routing headers added by
CM0+ at TX time from the Routing State. Pulled by CM0+ when a Cluster_Exchange
TX slot is active and the Cell Permit grants it. C1, C2, C3.

### ClusterBuffer
RX Buffer for `Cluster_Exchange` Phase. Written by CM0+ on reception. Read by
CM4 for routing decisions and application processing. C1, C2, C3.

### BeaconPayload
Single structured packet slot for `Mesh_Beacon` Phase. Not a queue — CM4
overwrites it each time routing state changes; CM0+ reads it once per Beacon
Phase. Contains `hop_count` (this node's depth in the mesh, used by receivers
to derive their own hop count) and up to 4 routes each with a `node_id` and
`route_cost`. Exact wire structure to be determined. C2/C3 only.

### Hop-Count Cell Eligibility
Within `Mesh_Uplink`, a C2 node only transmits in the Cell whose index satisfies
`cell_index % 3 == hop_count % 3`. This staggers relay traffic: nodes one hop
from C3 use Cell 0, two hops Cell 1, three hops Cell 2, four hops back to Cell 0.
Prevents all relay nodes competing for the same Cell. The node's `hop_count` is
read from the last received `BeaconPayload`. C3 (hop 0) transmits in every third
Cell starting from Cell 0.

---

## Mesh Routing

### Route Entry
Each candidate upstream peer is stored as a triple `(node_id, hop_count, route_cost)`.
`hop_count` is the peer's mesh depth (receiver sets its own to `peer.hop_count + 1`).
`route_cost` is a **cumulative path cost**: each node computes its own as
`parent.route_cost + f(energy, load)`, where `f` is a local function of the node's
remaining energy and current load. The value published in `BeaconPayload` is the
node's accumulated path cost from root; receivers add their own `f(energy, load)` to
derive their own `route_cost` for forwarding. Both `hop_count` and `route_cost` are
strictly increasing along any path from root — this invariant makes the anti-circular
filter reliable. `load` is the node's current TX queue depth (higher backlog = higher
cost, discourages routing through congested nodes). Exact formula for `f` is not yet
specified.

### Route Selection Policy
A C2 selects the route with the **lowest `route_cost`**. On a tie, it selects the
**lowest `hop_count`**. This is evaluated at every `Mesh_Beacon` Phase when CM4
updates the Routing State.

### Route Update
Because `BeaconPayload` carries `node_id`, a C2 can update the `route_cost` of an
already-known peer when a fresher beacon arrives. This allows C2 to detect battery
depletion on its current upstream and switch routes proactively.

### Anti-Circular Route Filter
When a new `BeaconPayload` arrives, C2 discards it if both conditions hold:

```
new.hop_count == current_selected.hop_count + 1
AND
new.route_cost > current_selected.route_cost
```

A beacon satisfying both conditions originates from a node one level downstream
that has a higher cost than the current upstream. Accepting it would create a
routing loop in the event of peer reselection (e.g., the downstream node previously
chose this node as its upstream and is now being considered as an upstream in return).
The strictly-increasing invariant on both fields makes this filter sufficient to
prevent circular exchanges.

### Pairing via Beacon
A C2 transitions from `Synchronized` to `Paired` upon receiving its first valid
`BeaconPayload` that passes the route filter. No handshake. The received route is
written to the Routing State immediately; CM0+ uses it for the next TX assembly.

---

## Cluster Protocol

### Cell Permit
CM4's decoded conclusion after reading the Cluster_Exchange header payload. States
which cell(s) this node is permitted to use for uplink in the current
`Cluster_Exchange` Phase. Written by CM4 to shared memory; read by CM0+ at each
cell opportunity to decide whether to transmit or sleep. Cluster topology only.

If CM4 has not yet written the Cell Permit by the time the first cell opportunity
fires (e.g., CM4 was busy with sensor acquisition), CM0+ sees no grant and skips
that cell. CM0+ re-checks at every subsequent opportunity — missing early cells is
acceptable by design.

The Cluster_Exchange header slot must therefore include a mandatory gap before the
first C1 uplink cell, sized to accommodate: CM0+ flagging CM4, CM4 finishing any
in-flight sensor work, CM4 decoding the header, and CM4 writing the Cell Permit to
shared memory.

---

## CM4 Sensor Acquisition Model

### MbMux CPU Budget

Due to IPCC channel serialisation and TDMA schedule coupling, the worst-case
interval between two consecutive MbMux Application Signal Channel notifications
is **20–30 ms**. CM4 must complete all ISR processing and shared-memory writes
within this window. This is the governing constraint for all CM4 services.

### Sensor Interface CPU Burden

| Interface | DMA for data | CPU-free during transfer? | Caveat |
|---|---|---|---|
| SPI | Yes | Essentially yes | Software CS toggle — microseconds only |
| USART / RS-485 data | Yes | Yes | UART IDLE interrupt for variable-length Rx |
| RS-485 DE/RE | N/A | Yes | STM32 hardware DE auto-toggle — no GPIO bit-bang needed |
| ADC | Yes | Yes | Continuous scan → circular DMA buffer, zero CPU per sample |
| I2C | Yes | Mostly | DMA moves bytes; START/STOP/NACK managed by I2C peripheral + short IRQ |
| SDI-12 | Partial | No — protocol requires CPU staging | See SDI-12 State Machine below |

**Key distinction:** DMA eliminates per-byte CPU intervention; it does not
eliminate the ~microsecond DMA Transfer Completion ISR. Those ISRs are too short
to threaten the 20–30 ms budget. What consumes the budget is **polling mode**:
`HAL_I2C_Master_Receive`, `HAL_ADC_PollForConversion`, or any busy-wait loop.
All sensor interfaces must use DMA + completion callbacks; polling mode is
prohibited.

### DMA Transfer Completion ISR
The short (~microsecond) interrupt fired by the DMA controller when a transfer
finishes. Does not block the 20–30 ms MbMux CPU budget. The permissible CM4
interrupt form — as opposed to polling mode which does block the budget.

### SDI-12 State Machine
SDI-12 mandates a fixed wakeup sequence: pull line low for ≥12 ms (break), then
mark for 8.33 ms, then transmit at 1200 baud. The 12 ms break cannot be handed
to DMA — it is an inherently timed state. Implementation: a hardware timer ISR
sequences the four stages: `BREAK → MARK → DMA-Tx → DMA-Rx`. CPU involvement is
limited to short ISRs at each state transition; no polling loop. SDI-12 is the
sole sensor interface that requires a timer-driven state machine rather than pure
DMA.

**SDI-12 and the MbMux window:** the 12 ms break overlaps with the 20–30 ms
budget if an MbMux notification fires during a SDI-12 command cycle. The CM4
scheduler must account for this: either (a) SDI-12 commands are never issued
within a Phase where MbMux traffic is expected, or (b) the SDI-12 state machine
is interruptible at stage boundaries and can defer its next stage.

---

## Non-Volatile Memory (NVM)

Two NVM backends coexist on the board:

| Backend | Medium | Interface | CS | Owner |
|---|---|---|---|---|
| Internal Flash | STM32WL on-chip flash | Memory-mapped (CPU write) | N/A | CM4 |
| External Flash | External SPI NOR flash | Dedicated SPI peripheral + GPIO software CS | Software-toggled GPIO | CM4 |

**Internal Flash** stores boot-time provisioning constants only (device EUI, join
keys, node class, calibration offsets). Written once at factory; never erased in
the field. No MbMux budget conflict.

**External Flash** stores runtime data (sensor logs near-term; OTA firmware
images as a future path). Uses an **append-only circular buffer** over pre-erased
sectors. Sector erases are issued during idle windows only — never in the sensor
acquisition hot path.

### External Flash State Machine
Non-blocking SPI driver following the same timer-driven pattern as the
SDI-12 State Machine. CM4 issues an erase or page-program command over SPI DMA
(microseconds of bus time), then returns to Stop2. On each subsequent CM4 wake
(for any reason — Alarm B or MbMux), CM4 checks a persistent flash operation
state variable. If a flash operation is pending, it reads the flash STATUS
register over SPI to check the BUSY flag. If ready, it advances to the next
stage; if still busy, it returns to Stop2. No dedicated wake source is required
for flash polling — status is checked opportunistically on every CM4 wake.

The external flash SPI peripheral is dedicated — no sensor shares this bus.
Flash DMA and sensor DMA streams are fully independent.

---

## Watchdog and Core Liveness

STM32WL has a single hardware IWDG shared by both cores. Per-core hardware
watchdogs do not exist. Liveness is split by role:

| Mechanism | Protects | Owner | Timeout |
|---|---|---|---|
| IWDG | CM0+ | CM0+ kicks on every TDMA slot wake | 2× max inter-slot gap (seconds range) |
| CM4 Heartbeat | CM4 | CM4 writes a counter to shared memory on every wake; CM0+ checks it | N consecutive unanswered MbMux notifications |

**CM4 Heartbeat:** CM4 increments a shared memory counter on every wake (Alarm B
or MbMux). CM0+ monitors this counter when processing Application Signal Channel
events. If CM4 fails to respond to N consecutive MbMux notifications, CM0+
resets CM4 independently via RCC (STM32WL supports isolated CM4 reset without
disturbing CM0+ or the radio state).

Detection latency for a CM4 hang is bounded by the MbMux notification rate —
worst case, the first notification after the hang is detected. During long sensor
sleep gaps (hours), CM4 hangs go undetected until the next MbMux event fires.
This is acceptable for multi-year field deployments where a few-hour detection
window is tolerable.

The hardware IWDG is **not used in production**. LSI is not enabled. Rationale:
IWDG maximum timeout (~32 s) is incompatible with hour-long inter-Phase sleep
gaps — satisfying it requires periodic sub-alarms that cost more energy than the
protection is worth for this deployment profile.

### Debug Console
CM4 owns a UART peripheral driven by DMA ring buffer. Present in all builds
(debug and production). CM4 enqueues log entries into a ring buffer; DMA drains
asynchronously — zero CM4 blocking time. Active only during CM4 wake windows.

### Trace Channel (CM0+ → CM4)
Implemented via STM32 middleware: `mbmuxif_trace` + `stm32_adv_trace` utility.

CM0+ writes into the `stm32_adv_trace` circular FIFO (zero-copy allocation via
`UTIL_ADV_TRACE_ZCSend_Allocation/Finalize`), then fires
`MBMUXIF_TraceSendNotif_NoWait()` — no blocking, no ACK wait. The FIFO absorbs
bursts; multiple trace writes batch into one IPCC notification. CM4 receives the
notification, drains the entire FIFO to UART DMA in one wake, then calls
`MBMUXIF_TraceSendAck()`. Buffer-full overrun is handled via the registered
overrun callback. IPCC pressure is naturally rate-limited by the ACK mechanism.
Budget impact: FIFO drain to DMA is microseconds — compatible with the 20–30 ms
MbMux window. Verbose level and region filtering available via
`UTIL_ADV_TRACE_COND_FSend` to suppress trace categories at runtime.

---

**Deferred [NEXT PRIORITY] — TDMA Table class refactoring:** the current table structure encodes per-class slot patterns via `cell_bitmap[3]` and `participant_mask`, but C3's distinct slot pattern and the MAC State Machine's class-specific mechanisms (contention eligibility, hop-count mod-3, Cell Permit) likely require deeper structural changes. Full redesign needed before implementation.

**Deferred [NEXT PRIORITY] — Sensor scheduling machine:** how CM4 tracks multiple sensor descriptors with independent periods, computes the minimum-next-alarm across all due sensors on each Alarm B wake, and handles coincident sensors in one acquisition cycle. `AlarmBRequest.pending` flag is settled (Option B); scheduling logic is not yet specified.

**Deferred [NEXT PRIORITY] — Sensor power management:** whether sensor interfaces require a switched power rail before acquisition, and the worst-case sensor startup time to accommodate in the TDMA header gap. Initial assumption: switched power rail controlled by a hardware GPIO (always-on not affordable). Awaiting hardware confirmation from colleague.

**Deferred — All `Cluster_Exchange` internals:** header content and wire format, Cell Permit encoding, per-node cell assignment model (`Cluster_Join` phase, join request packet, acceptance policy, cluster capacity limits, MAC State Machine integration). None of this is a current priority.

**Deferred — Factory provisioning:** how boot constants are written to internal flash at manufacturing (SWD programmer, UART bootloader, or other) is not yet decided.

**Deferred — CM0+ hang detection:** if CM0+ hangs, it stops sending MbMux
signals; TX queue entries age indefinitely. A future mechanism — CM4 triggering a
system reset via `SCB_AIRCR` when payload age exceeds N frames — is a viable
path. Not a priority until core TDMA and sensor mechanisms are operational.
