## Problem Statement

The ArcLoRaM node has no protocol behaviour. CM0+ knows how to access the radio hardware and run a cooperative task loop, but has no timing engine, no schedule to follow, and no per-slot decision logic. CM4 has no radio event handlers, no sensor scheduling, and no inter-core protocol reactions. The STM32WL dual-core SoC is powered on but does nothing useful.

**Radio path:** CM0+ owns the radio hardware and talks to it directly through the radio middleware (`Radio` vtable). No MbMux proxy is involved for CM0+ radio operations.

## Solution

Implement five protocol machines as self-contained C modules: four on CM0+, one stubbed on CM4. Each fits the existing `UTIL_SEQ` cooperative-task framework. TDMA slot timing is driven by **RTC Alarm A** (not UTIL_TIMER — UTIL_TIMER uses the RTC wakeup timer, which is a separate resource). Synchronisation state is managed entirely within CM0+ — the MAC State Machine transitions are driven by received radio packets, not by MbMux messages. MbMux signals (`SYNC_LOCKED`, `SYNC_LOST`) are fired to inform CM4 for diagnostics, not to drive CM0+ state.

## User Stories

1. As a C1, C2, or C3 node, I want to wake precisely at my assigned TDMA slot boundary so that my radio window never overlaps a neighbour's.
2. As a C1 or C2 node, I want to return to deep Stop2 sleep immediately after my slot so that I preserve battery across multi-hour inter-phase gaps. (C3 does not enter Stop2.)
3. As a node of any class, I want the TDMA Machine to skip phases whose `participant_mask` excludes my node class at compile time so that I never waste energy on irrelevant phases.
4. As a C2 relay, I want the MAC State Machine to compute and cache my `CellEligibilityMask` after each beacon reception so that the TDMA Machine has the mask ready at every cell boundary without waiting for CM4.
5. As a C1 or C2 node, I want the MAC State Machine to manage my state transitions (Scanning to Synchronized to Paired) entirely within CM0+ so that state changes happen atomically in the cooperative task loop without cross-core latency.
6. As a C3 gateway (SyncAnchor), I want to boot directly into `Active` state with no three-packet sync acquisition required. Note: C3 will eventually run a GPS-based (or equivalent) external sync sequence — that acquisition is deferred, but the state machine must leave a hook for it.
7. As a C3 gateway, I want the MAC State Machine to bypass `BeaconTxBudget` and transmit in every eligible Mesh_Beacon cell unconditionally so that downstream C2 nodes always have an anchor signal.
8. As a C2 relay participating in CT Sync, I want the MAC State Machine to use the three-tier per-occurrence dispatch (Rx Cell 0, relay Cells 1+ if epoch received with acceptable error) so that I can measure my drift against the incoming epoch and relay it in a single Sync Phase occurrence. (Audit cycle alternation is deferred — part of the pure CT model.)
9. As a C1 or C2 node in `Scanning`, I want continuous wide-RX mode with no TX so that I do not violate duty-cycle rules before acquiring a frame epoch.
10. As any node, I want the Frequency Resolver to produce `freq_hz` for every slot (header, cells, footer) by reading `FrequencyResolverState` from shared memory so that CM4 can update channel assignments without touching CM0+ code.
11. As any node, I want the Frequency Resolver to support three cell-frequency modes (STATIC, HOP, OVERRIDE) so that different phases can use fixed channels, frequency hopping, or per-cell channel tables.
12. As any TX node, I want the Compliance Engine to gate every transmission through `RequestChannel()` so that my duty-cycle credit never goes negative.
13. As any TX node, I want the Compliance Engine to deduct accurate airtime via `ReportTxDone()` using the radio driver computed `TimeOnAir` so that I do not over-restrict myself relative to the conservative `slot_active_ms` pre-check.
14. As an operator, I want CM0+ to write `ComplianceStatus` to shared memory after every `RequestChannel` call so that CM4 can read skip counts and trigger a `DIAG_ALARM` on band exhaustion without any new MbMux signal.
15. As a node in a CONTENTION footer slot, I want the MAC State Machine to treat a `RESTRICTED` result from the Compliance Engine as a lower bound on CSMA backoff, reusing the `remaining_slot_time` tracking already required for CSMA.
16. As a SCHEDULED-slot TX node, I want any non-`GRANTED` result from the Compliance Engine to cause a slot skip and a `ComplianceStatus` update so that the RTC alarm chain continues without interruption.
17. As CM4, I want a Sensor Scheduler stub (returning a fixed 10-minute period) so that the `AlarmBRequest` inter-core contract is exercised from day one, even before real sensor descriptors are wired in.
18. As CM4, I want to receive `SYNC_LOCKED` from CM0+ and immediately write an `AlarmBRequest` so that sensor acquisition begins on the first frame after synchronisation.
19. As CM4, I want to receive `TX_NO_ACK` with a destination peer ID and increment that peer failure counter in the Routing State so that CM0+ uses the updated route cost at the next packet assembly.
20. As CM4, I want to receive `ACK_RECEIVED` with a sequence number and remove the matching entry from the TX queue so that the queue never grows with already-delivered payloads.
21. As an operator, I want the FrameCursor to include a double-check or fallback mechanism so that a single bad alarm or missed wake cannot permanently desynchronise the schedule — the cursor must be recoverable.
22. As any node, I want the alarm-chain model to program the next RTC Alarm A before returning to sleep so that a single missed wake never permanently desynchronises the schedule.
23. As a developer, I want each machine to be registered as an independent `UTIL_SEQ` task so that I can pause or isolate any machine during integration testing without modifying other tasks.

## Implementation Decisions

### Module Decomposition

| Machine | Core | Module | Status |
|---|---|---|---|
| TDMA Machine | CM0+ | `tdma_machine` | Implement |
| MAC State Machine | CM0+ | `mac_state_machine` | Implement |
| Frequency Resolver | CM0+ | `freq_resolver` | Implement |
| Compliance Engine | CM0+ | `compliance_engine` | Implement |
| Sensor Scheduler | CM4 | `sensor_scheduler` | Stub only |

- All CM0+ machines: `CM0PLUS/SubGHz_Phy/App/`
- CM4 stub: `CM4/SubGHz_Phy/App/`
- TDMA Table (read-only, both cores): `Common/Protocol/tdma_table.h`
- Shared memory structures: `Common/SharedMemory/shared_mem.h`

### TDMA Machine — RTC Alarm A + UTIL_SEQ

**Timing source:** RTC Alarm A (not UTIL_TIMER). UTIL_TIMER uses the RTC wakeup timer — a separate resource. RTC Alarm B is dedicated to CM4 sensor acquisition. These three resources must not be shared.

Registers `CFG_SEQ_Task_TdmaSlotWake`. RTC Alarm A ISR fires, wakes core from Stop2, calls `UTIL_SEQ_SetTask()`. Task context slot cycle:

```
1. Read FrameCursor
2. Check participant_mask — if excluded, jump to next active phase
3. FrequencyResolver_GetFreq(cursor) -> Radio.SetChannel()
4. MAC_OnSlotOpportunity(cursor, phase) -> SlotDecision {TX, RX, SKIP}
5. If TX: ComplianceEngine_RequestChannel() -> GRANTED / skip
6. Execute radio action (Radio.Send / Radio.SetRx / Radio.Sleep)
7. Advance FrameCursor
8. next_alarm = slot_start + slot_active_ms + gap_slots_ms[slot_index] - (next_slot_is_rx() ? GUARD_TIME_MS : 0)
9. Program RTC Alarm A (absolute RTC time)
-> return -> UTIL_SEQ_Run() -> UTIL_LPM -> Stop2
```

**Guard Time on RX:** Alarm A programmed `GUARD_TIME_MS` early. The Rx window ends at the latest packet start, `slot end + MAX_GUARD_TIME_MS - ToA` (superseded the former `slot_active_ms + 2 x GUARD_TIME_MS`, see ADR-0015).

**FrameCursor integrity (Story 21):** A cursor checkpoint (RTC timestamp) stored at every slot completion. On each Alarm A wake, the machine validates the delta is plausible. Implausible delta marks `CURSOR_SUSPECT` and initiates re-sync (`CLOCK_ACQUIRING`). Soft fallback — does not hard-reset sync state on small deviations.

### MAC State Machine — CM0+-internal, no MbMux dependency

Entirely CM0+-internal. State transitions driven by radio callbacks received directly on CM0+. MbMux signals (`SYNC_LOCKED`, `SYNC_LOST`) are fired to CM4 for diagnostics only — they do not drive the CM0+ state machine.

Not a separate `UTIL_SEQ` task. Synchronous call from TDMA task:

```c
SlotDecision_t MAC_OnSlotOpportunity(const FrameCursor_t *cursor, const Phase_t *phase);
```

Radio callback hooks (CM0+ RxDone wrapper):

```c
void MAC_OnSyncPacketReceived(const SyncPayload_t *p, uint32_t stamp_ms);
void MAC_OnBeaconReceived(const BeaconPayload_t *b);
```

`CellEligibilityMask` pre-computed and written to shared memory inside these callbacks — always ready before TDMA reads them at slot boundary. (`phase_tx_flag` is retained in shared memory but currently unused — see `DIRECTION_MAC_PHASE` in `CONTEXT.md`.)

C3 external sync hook (GPS/equivalent, deferred):

```c
void MAC_OnExternalSyncAcquired(const FrameEpoch_t *e);
```

### Frequency Resolver — pure function

```c
uint32_t FrequencyResolver_GetFreq(const FrameCursor_t *cursor);
```

No UTIL_SEQ task, no UTIL_TIMER. Called synchronously from TDMA task. CM0+-local LFSR for `CELL_FREQ_HOP` mode. `FrequencyResolverState` 400 bytes + override pool 640 bytes = 1040 bytes in SRAM2 (`MAX_PHASES=20`, `MAX_CELLS_PER_PHASE=32`, `MAX_OVERRIDE_TABLES=5`). Override tables are split out of the per-phase struct into a separate pool — only phases using `CELL_FREQ_OVERRIDE` consume entries.

### Compliance Engine — synchronous hooks, shared-memory output

```c
ComplianceResult_t ComplianceEngine_RequestChannel(uint32_t freq_hz,
                                                    uint32_t expected_toa_ms,
                                                    int8_t   tx_power_dbm);
void               ComplianceEngine_ReportTxDone(uint32_t freq_hz,
                                                  uint32_t actual_toa_ms);
```

- `expected_toa_ms` = `slot_active_ms` (conservative pre-TX)
- `actual_toa_ms` = `Radio.TimeOnAir(...)` from `radio.h:268` (accurate post-TX deduction)
- Writes `ComplianceStatus` to shared memory after every `RequestChannel` — CM4 reads opportunistically
- `RegionProfile` compiled into CM0+ flash as read-only constants; no runtime region swap
- `BAND_UNKNOWN`: assert in debug; silent skip + `band_unknown_count++` in production

### Sensor Scheduler — CM4 stub (deferred)

Stub registers `CFG_SEQ_Task_SensorSchedulerStep` and always writes an `AlarmBRequest` 10 minutes ahead. Exercises `AlarmBRequest` inter-core contract end-to-end. Stub interface = final interface; only the implementation changes later.

Future real implementation: multi-sensor `SensorDescriptor` table, coincident batching, DMA-only interfaces, SDI-12 timer state machine.

### TDMA Table

```c
const Phase_t  *TdmaTable_GetPhase(uint8_t phase_index);
uint8_t         TdmaTable_PhaseCount(void);
uint32_t        TdmaTable_PhaseStartOffset_ms(uint8_t phase_index);
```

`const` array in flash. Node class = compile-time `NODE_CLASS` define. Excluded phases = zero-cost jump.

### UTIL_SEQ Task ID Allocation

| Task | Core | Purpose |
|---|---|---|
| `CFG_SEQ_Task_TdmaSlotWake` | CM0+ | TDMA slot processing |
| `CFG_SEQ_Task_MbAppSignalRcv` | CM0+ | Dispatch MbMux app signals to MAC hooks |
| `CFG_SEQ_Task_SensorSchedulerStep` | CM4 | Stub sensor scheduler |

TDMA task priority lower than MbMux radio command tasks.

### Shared Memory Additions (`Common/SharedMemory/shared_mem.h`)

- `AlarmBRequest` — pending flag + BCD RTC time fields
- `CellEligibilityMask` (uint8_t, 3-bit)
- `phase_tx_flag` (uint8_t) — retained, currently unused (`DIRECTION_MAC_PHASE` not used by any phase)
- `FrequencyResolverState` (400 bytes, 20 phases x 20 bytes) + `g_freq_override_tables` pool (640 bytes, 5 x 32 cells x 4 bytes)
- `ComplianceStatus` (12 bytes)
- `RoutingState` — array of RouteEntry
- `MeshUplinkQueue`, `MeshDownlinkQueue`, `ClusterQueue` — 3-tier priority TX queues
- `MeshUplinkBuffer`, `MeshDownlinkBuffer`, `ClusterBuffer` — RX buffers
- `BeaconPayload` — single-entry
- `EmergencyAlertSlot` — single-entry DiagnosticPayload

**Race-freedom:** `AlarmBRequest.pending` and `ComplianceStatus.last_result` are `uint8_t` — naturally atomic on ARM Cortex-M. No mutex required.

## Testing Decisions

Good tests verify observable outputs (shared memory writes, Radio vtable calls, public state accessor) given controlled inputs. Never test internal counters — test that the machine made the correct radio call or wrote the correct shared memory value.

| Module | Test type | What to verify |
|---|---|---|
| TDMA Machine | Unit (cursor arithmetic) | Alarm A delta correctness for all phase/cell/slot combinations; phase-skip for excluded classes; frame wraparound; `CURSOR_SUSPECT` fires on implausible delta |
| MAC State Machine | Unit (state machine) | All state transitions; `CellEligibilityMask` correctness per hop-count (uplink + downlink formulas); K-of-N Beacon cell selection; three-packet sync reaches `CLOCK_WARM` |
| Frequency Resolver | Unit (pure function) | STATIC, HOP, OVERRIDE modes; absent header/footer returns 0 |
| Compliance Engine | Unit (time-credit) | GRANTED/RESTRICTED/POWER_TOO_HIGH; credit deduction by `ReportTxDone`; `ComplianceStatus` written after every call |
| Sensor Scheduler stub | Integration | `AlarmBRequest.pending` set after time fields; always returns 10-min offset |

Testability: `Radio` vtable replaced with a mock in test builds. Compliance Engine and Frequency Resolver have no radio dependency — pure-C unit tests, no hardware required.

## Out of Scope

- C3 external time acquisition (GPS/equivalent) — hook exists, implementation deferred
- `Cluster_Exchange` internals (Cell Permit, join request, cluster capacity)
- Variable TX power scheme for CT Sync (pending empirical validation)
- `DIAG_CMD_PARAM_UPDATE` and `DIAG_CMD_SELFTEST` (pending operator confirmation)
- Full Sensor Scheduler (multi-sensor arithmetic, DMA callbacks, SDI-12 state machine)
- Factory provisioning path
- CM0+ hang detection / CM4-triggered system reset
- External flash log format
- OTA firmware update path

## Further Notes

- **RTC resource allocation:** Alarm A = TDMA timing (CM0+). Alarm B = sensor acquisition (CM4 via AlarmBRequest). RTC wakeup timer = UTIL_TIMER. Never share these three resources.
- **C3 sleep:** Compile-time `NODE_CLASS` constant makes Stop2 vs Run/Sleep a zero-cost compile-time branch.
- **`actual_toa_ms` source:** `Radio.TimeOnAir(modem, bw, dr, cr, preambleLen, fixLen, payloadLen, crcOn)` at `radio.h:268`, called after `SetTxConfig` when payload length is known.
