# System Overview

> What lives on which core, what calls what, and where data crosses the inter-core boundary.

```mermaid
flowchart LR
    subgraph CM0["CM0+ — Radio & Protocol"]
        direction TB
        RTC["RTC Alarm A"]
        SEQ["UTIL_SEQ scheduler"]
        TDMA["TDMA Machine<br/><i>tdma_machine.c</i>"]
        MAC["MAC State Machine<br/><i>mac_state_machine_cX.c</i>"]
        FREQ["Frequency Resolver<br/><i>freq_resolver.c</i>"]
        COMP["Compliance Engine<br/><i>compliance_engine.c</i>"]
        RADIO["SubGHz Radio<br/>(LoRa PHY)"]

        RTC -- "ISR wakeup" --> SEQ
        SEQ -- "SlotTask()" --> TDMA
        TDMA -- "GetFreq()" --> FREQ
        TDMA -- "OnSlotOpportunity()" --> MAC
        TDMA -- "RequestChannel()" --> COMP
        TDMA -- "Send / Rx / Sleep" --> RADIO
        RADIO -. "RxDone callback" .-> MAC
    end

    subgraph SRAM2["Shared SRAM2"]
        direction TB
        FRS["FrequencyResolverState_t<br/><i>CM4 writes, CM0+ reads</i>"]
        CS["ComplianceStatus_t<br/><i>CM0+ writes, CM4 reads</i>"]
        ABR["AlarmBRequest_t<br/><i>CM4 writes, CM0+ reads</i>"]
        EAS["EmergencyAlertSlot_t<br/><i>CM0+ writes, CM4 reads</i>"]
    end

    subgraph CM4["CM4 — Application"]
        direction TB
        SENSOR["Sensor Scheduler"]
        PAYLOAD["Payload Assembly"]
        ROUTE["Routing State"]
        DIAG["Diagnostics"]
        MBMUX["MbMux Signal Handler"]
    end

    FREQ -- "reads" --> FRS
    CM4 -- "writes" --> FRS
    COMP -- "writes" --> CS
    DIAG -- "reads" --> CS
    SENSOR -- "writes" --> ABR
    RADIO -- "writes" --> EAS

    CM0 == "IPCC signals:<br/>SYNC_LOCKED · SYNC_LOST<br/>RX_READY · TX_NO_ACK" ==> MBMUX
```

## How to Read This

1. **Start at RTC Alarm A** (top-left): every slot begins with an interrupt
2. **Follow the arrows down** through TDMA Machine — it orchestrates each slot
3. **Three queries per slot**: frequency lookup, MAC decision, compliance gate
4. **Radio action**: the result of the MAC decision (TX, RX, or Sleep)
5. **Dashed arrow**: asynchronous radio callbacks feed back into the MAC
6. **Shared SRAM2** (center): strict single-writer ownership, no mutexes
7. **IPCC signals** (thick arrow): event notifications from CM0+ to CM4
