# ArcLoRaM Domain Language

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
| `Mesh_Downlink` | Mesh | Gateway-to-node controlled flooding |
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
is applied by the Protocol State Machine — it is not a separate Slot type.

### Slot Kind
Qualifies how a Slot is accessed:
- `SCHEDULED`: direction is predetermined by the Cell blueprint (Tx or Rx).
- `CONTENTION`: any eligible node may attempt to transmit after channel sensing
  and random backoff. The Protocol State Machine decides Tx vs Rx at runtime.
  Cell blueprint does not apply.

### Phase Header Slot
An optional single Slot anchored to the **start** of a Phase, outside the Cell × N
repetition. Has its own `duration_ms` and per-class bitmap. Used for management
or control frames (e.g., cluster management packet at the start of Cluster_Exchange).
Absent when `duration_ms == 0`.

### Phase Footer Slot
An optional single Slot anchored to the **end** of a Phase, outside the Cell × N
repetition. Has its own `duration_ms` and Slot Kind. In `Cluster_Exchange` this
is a CONTENTION slot: nodes sense the channel, then attempt to transmit either a
data packet or a join request. Both imply an ACK from the cluster master.
The Protocol State Machine handles packet type selection, sensing, and backoff entirely.
Absent when `duration_ms == 0`.

### Slot Active Duration
A per-Phase field (`slot_active_ms`) representing the worst-case duration of one
complete Slot exchange (DATA+ACK pair, or a single packet for Beacon/Sync Phases).
Actual airtime may be shorter as modulation parameters vary, but the alarm chain
always uses `slot_active_ms` to guarantee no overlap. The next alarm is programmed
as `slot_start + slot_active_ms + gap_after_ms[i]`.

### Radio State
The hardware operating state of the radio chip: `Tx | Rx | Sleep`.
Maps directly to SX127X / STM32WL PHY states. A hardware-layer concept — not
to be confused with Phase Type (MAC layer).

### Guard Time
A global protocol constant (`GUARD_TIME_MS`). Not stored in the TDMA Table.
Applied by the Protocol State Machine: whenever a node enters Rx mode, it opens
the receive window `GUARD_TIME_MS` before the nominal Slot start and holds it
until `GUARD_TIME_MS` after the nominal end. Absorbs clock drift accumulated
since the last Frame Epoch correction.

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
and remain asleep for its entire duration. The Protocol State Machine checks this
at Phase entry before programming any alarms.

Example: `Mesh_Uplink` has `participant_mask = 0b110` (C2 + C3 only; C1 sleeps
through the entire Phase).

### Protocol State Machine
Consults the TDMA Table via a single accessor. Never modifies it. All dynamic,
node-specific behavior lives here — the table is inert data.

**TDMA Table responsibilities** (what the table owns):
- Which Phase Types appear and in what order
- Which Node Classes participate in each Phase (`participant_mask`)
- The Rx/Tx pattern per Node Class per Slot (`cell_bitmap`)
- Slot active duration and inter-slot gaps (`slot_active_ms`, `gap_after_ms`)
- Phase Header and Footer Slot definitions
- Cell repetition count (`cell_count`)

**Protocol State Machine responsibilities** (what the table does NOT own):
- Advancing the Frame Cursor and computing absolute slot times
- Checking `participant_mask` at Phase entry — if local class is excluded, skip
  Phase entirely and program alarm for next Phase start
- Programming RTC alarms for each wake-up (alarm-chain sleep model)
- Applying Guard Time on Rx Slots
- Applying hop-count mod-3 eligibility within Mesh_Uplink
- Executing contention logic in CONTENTION footer Slots (CSMA + backoff)
- Selecting packet type (data vs join request) in Cluster_Exchange footer
- Correcting Frame Epoch on Mesh_Beacon or Sync reception

**Sleep strategy — alarm-chain model:**
Nodes are always in deep sleep between Slots. There is no idle polling or
spin-waiting. After completing Slot i, the Protocol State Machine computes:

  `next_alarm = slot_start(i) + slot_active_ms + gap_after_ms[i]`

and programs the RTC before returning to sleep. On the next wake, it resumes
from the updated Frame Cursor. For long inter-Phase gaps (hours), the state
machine jumps the cursor directly to the next active Slot for the local Node
Class, using the precomputed `phase_start_offset[]` array to avoid iterating
through intermediate Phases and Cells.

**Key boundary**: TDMA Table = schedule data (read-only, node-agnostic).
Protocol State Machine = behavior (node-aware, alarm-driven).

### Frame Cursor
The Protocol State Machine's live position within the TDMA Table.
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
Index of the `Sync` Phase slot that transmitted this packet.
`0` = SyncAnchor's direct transmission; `n` = forwarded by the n-th relay hop.
All `Sync` Phase slots are uniform duration. The receiver uses this field to
compute the exact preamble arrival offset from frame start:

```
expected_offset_ms = preceding_phases_ms
                   + sync_slot_index × (sync_slot_active_ms + sync_slot_gap_ms)
```

### PreambleStamp
An in-memory RTC snapshot (`RTC_TimeTypeDef`) captured inside the DIO1 GPIO ISR
the instant a sync preamble is detected. Never persisted. Consumed immediately
by the warm-sync handler to compute `elapsed_ms`.

### ClockState
Two-value enum tracking whether the node's RTC has been calibrated by a sync
packet.

| Value | Meaning |
|---|---|
| `CLOCK_COLD` | RTC not yet set from any sync packet (boot default) |
| `CLOCK_WARM` | RTC calibrated; warm-sync algorithm is active |

### Sync Algorithm — summary

**Cold start (`CLOCK_COLD`):** parse `SyncPayload` → call `HAL_RTC_SetTime` /
`HAL_RTC_SetDate` with BCD fields directly (no conversion) → set Frame Cursor to
start of `Sync` Phase → `ClockState = CLOCK_WARM`. Sub-second precision is lost;
cursor is approximate until the second packet.

**Warm sync (`CLOCK_WARM`):** capture `PreambleStamp` in DIO1 ISR → parse
`SyncPayload` → compute `elapsed_ms = rtc_to_ms(PreambleStamp) − rtc_to_ms(FrameEpoch)` →
walk TDMA Table to derive Frame Cursor → compute `clock_error_ms` from
`expected_offset_ms` and correct RTC.
