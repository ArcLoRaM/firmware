# Inter-Core Communication

> Strict single-writer ownership = race-freedom without mutexes. Each shared memory region has exactly one writer core.

## Data Flow Overview

```mermaid
flowchart LR
    subgraph CM4["CM4 — Application Core"]
        direction TB
        SENSOR["Sensor Scheduler"]
        ROUTING["Routing State"]
        DIAG["Diagnostics"]
        MBHANDLER["MbMux Signal Handler"]
    end

    subgraph SRAM2["SRAM2 Shared Memory (0x20009000)"]
        direction TB
        ABR["AlarmBRequest_t<br/><i>12 bytes</i><br/><b>CM4 → CM0+</b>"]
        CS["ComplianceStatus_t<br/><i>16 bytes</i><br/><b>CM0+ → CM4</b>"]
        HB["CM4 Heartbeat Counter<br/><i>4 bytes</i><br/><b>CM4 → CM0+</b>"]
        EAS["EmergencyAlertSlot_t<br/><i>12 bytes</i><br/><b>CM0+ → CM4</b>"]
        FRS["FrequencyResolverState_t<br/><i>~980 bytes</i><br/><b>CM4 → CM0+</b>"]
    end

    subgraph CM0["CM0+ — Radio Core"]
        direction TB
        TDMA["TDMA Machine"]
        COMPLIANCE["Compliance Engine"]
        FREQRES["Frequency Resolver"]
        MAC["MAC State Machine"]
    end

    SENSOR -- "writes pending=1" --> ABR
    ABR -- "reads, programs Alarm B,<br/>clears pending=0" --> TDMA

    COMPLIANCE -- "writes after<br/>every RequestChannel()" --> CS
    CS -- "reads opportunistically" --> DIAG

    CM4 -- "increments on<br/>every wake" --> HB
    HB -- "monitors for<br/>liveness" --> MAC

    MAC -- "writes on CM4<br/>heartbeat failure" --> EAS
    EAS -- "clears on restart" --> CM4

    ROUTING -- "writes at boot +<br/>topology changes" --> FRS
    FRS -- "reads every slot" --> FREQRES

    CM0 == "IPCC Event Signals" ==> MBHANDLER
```

## IPCC Event Signals (CM0+ → CM4)

Single multiplexed Application Signal Channel. At most one signal per CM0+ wake.

| Signal | Trigger | CM4 Action |
|--------|---------|------------|
| `SYNC_LOCKED` | Clock reached CLOCK_WARM | Write AlarmBRequest if not pending |
| `SYNC_LOST` | Drift ≥ 100 ms, back to COLD | Record sync miss count |
| `RX_READY` | Packet reception complete | Read from phase-specific RX buffer |
| `TX_NO_ACK` | TX slot without ACK | Update peer failure counter |
| `ACK_RECEIVED` | TX slot with ACK | Dequeue delivered payload |
| `RX_TIMEOUT` | RX slot expired, no packet | Track link PER |

## Ownership Rules

```mermaid
flowchart TD
    RULE["Race-Freedom Design Principle"]
    RULE --> R1["Each struct has exactly<br/><b>one writer core</b>"]
    RULE --> R2["Atomic flag protocol<br/>for request/response<br/>(AlarmBRequest.pending)"]
    RULE --> R3["No mutexes or spinlocks<br/>ARM Cortex-M uint8_t writes<br/>are naturally atomic"]
    RULE --> R4["CM0+-internal state<br/>(CellEligibilityMask, PhaseTxFlag)<br/>stays in CM0+ RAM, not SRAM2"]

    style RULE fill:#e8eaf6
    style R1 fill:#c8e6c9
    style R2 fill:#c8e6c9
    style R3 fill:#c8e6c9
    style R4 fill:#c8e6c9
```

## Physical Memory Layout

All structs placed in linker section `SHARED_APP` at fixed address `0x20009000`:

| Offset | Structure | Size | Writer |
|--------|-----------|------|--------|
| `0x20009000` | `AlarmBRequest_t` | 12 B | CM4 |
| `0x2000900C` | `ComplianceStatus_t` | 16 B | CM0+ |
| `0x2000901C` | CM4 Heartbeat | 4 B | CM4 |
| `0x20009020` | `EmergencyAlertSlot_t` | 12 B | CM0+ |
| `0x2000902C` | `FrequencyResolverState_t` | ~980 B | CM4 |

## RTC Resource Allocation

The RTC is a shared peripheral with partitioned access:

| Resource | Owner | Purpose |
|----------|-------|---------|
| **Alarm A** | CM0+ (exclusive) | TDMA slot timing — never touched by CM4 |
| **Alarm B** | CM4 (via request) | Sensor scheduling — CM4 writes request, CM0+ programs hardware |
| **Wakeup Timer** | Independent | UTIL_TIMER — available to either core |
