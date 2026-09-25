# TDMA Slot Execution Flow

> The firmware heartbeat: every RTC Alarm A wake executes this exact pipeline in `TdmaMachine_SlotTask()`.

```mermaid
flowchart TD
    WAKE["RTC Alarm A fires"] --> READ_RTC["Step 1: Read RTC<br/><code>GetRtcMs()</code>"]

    READ_RTC --> INTEGRITY{"Step 2: Cursor Integrity<br/>|now − expected| > 1.5 × slot?"}
    INTEGRITY -- "Yes" --> SUSPECT["Mark CURSOR_SUSPECT<br/>Feed zero SyncPayload to MAC<br/>(force re-acquisition)"]
    INTEGRITY -- "No" --> CLEAR["Clear CURSOR_SUSPECT"]
    SUSPECT --> FETCH
    CLEAR --> FETCH

    FETCH["Step 3: Fetch Phase<br/><code>TdmaTable_GetPhase()</code>"]
    FETCH --> MASK{"Step 4: Participant Mask<br/>This node's class participates?"}
    MASK -- "No" --> SKIP_PHASE["Skip to next participating phase<br/>Program Alarm A → Sleep"]
    MASK -- "Yes" --> FREQ["Step 5: Frequency Resolve<br/><code>FrequencyResolver_GetFreq()</code>"]

    FREQ --> CHANNEL["Step 6: Set Radio Channel"]
    CHANNEL --> MAC_DEC["Step 7: MAC Decision<br/><code>MAC_OnSlotOpportunity()</code>"]

    MAC_DEC --> TX_PATH["SLOT_TX"]
    MAC_DEC --> RX_PATH["SLOT_RX"]
    MAC_DEC --> SKIP_PATH["SLOT_SKIP"]

    TX_PATH --> COMPLIANCE{"Step 8a: Compliance Check<br/><code>RequestChannel()</code>"}
    COMPLIANCE -- "GRANTED" --> SEND["Assemble SyncPayload<br/><code>RadioSend()</code><br/><code>ReportTxDone()</code>"]
    COMPLIANCE -- "RESTRICTED /<br/>POWER_TOO_HIGH" --> NO_TX["Skip TX<br/>(no transmission)"]

    RX_PATH --> RX["Step 8b: Open RX Window until latest packet start<br/><code>RadioSetRx(last - now, cap - now)</code><br/>last = slot end + MAX_GUARD - ToA, cap = slot end + MAX_GUARD"]
    SKIP_PATH --> SLEEP_RADIO["Step 8c: Radio Sleep"]

    SEND --> ADVANCE
    NO_TX --> ADVANCE
    RX --> ADVANCE
    SLEEP_RADIO --> ADVANCE

    ADVANCE["Step 9: Advance Cursor<br/><code>advance_cursor(phase)</code>"]
    ADVANCE --> NEXT["Step 10: Compute next slot start<br/><code>slot_start + slot_active + gap</code>"]
    NEXT --> GUARD{"Step 11: Guard Look-ahead<br/>Next slot is RX?"}
    GUARD -- "Yes" --> EARLY["Subtract guard_ms<br/><code>GuardTimeResolver_GetGuardMs()</code><br/>(100 ms, V1 constant)"]
    GUARD -- "No" --> PROGRAM
    EARLY --> PROGRAM

    PROGRAM["Step 12: Program RTC Alarm A"]
    PROGRAM --> STOP2["Return → Stop2 Sleep"]
```

## Cursor Advancement Logic

The TDMA Machine maintains a `FrameCursor_t {phase_index, cell_index, slot_index}` that traverses the schedule:

```mermaid
flowchart LR
    H["HEADER slot"] --> C["CELL slots<br/>(slot 0..N-1)"]
    C --> |"last slot,<br/>next cell"| C
    C --> |"last slot,<br/>last cell"| F["FOOTER slot"]
    F --> NP["Next Phase"]
    NP --> |"last phase"| WRAP["Frame wrap<br/>→ Phase 0"]

    style H fill:#e1f5fe
    style F fill:#e1f5fe
    style C fill:#fff3e0
```

## Key Constants

| Constant | Value | Used In |
|----------|-------|---------|
| `MAX_GUARD_TIME_MS` | 100 ms | RX slots wake early to catch preamble; also Tier 3 drift threshold |
| `TX_POWER_DBM` | 14 dBm | Compliance Engine power check |
