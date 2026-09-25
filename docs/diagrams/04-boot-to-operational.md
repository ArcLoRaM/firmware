# Boot to Operational Sequence

> How a C1/C2 node goes from power-on to fully paired. Shows initialization order and the three-packet Sync lock.

```mermaid
sequenceDiagram
    participant CM4 as CM4<br/>(Application)
    participant CM0 as CM0+<br/>(SubGhzPhyTask)
    participant MAC as MAC State<br/>Machine
    participant TDMA as TDMA<br/>Machine
    participant Radio as SubGHz<br/>Radio
    participant Net as RF Network

    Note over CM4,CM0: Power-on reset. CM4 boots first.
    CM4->>CM0: Release CM0+ core

    rect rgb(230, 245, 255)
        Note over CM0: Initialization sequence (SubGhzPhyTask_Init)
        CM0->>CM0: SharedMem_Init() — zero SRAM2
        CM0->>Radio: Radio.Init() — register callbacks
        CM0->>CM0: ComplianceEngine_Init()
        CM0->>CM0: FrequencyResolver_Init()
        CM0->>MAC: MAC_Init()<br/>state = SCANNING, clock = COLD
        CM0->>TDMA: TdmaMachine_Init()<br/>cursor = {0, 0, 0}
        CM0->>TDMA: TdmaMachine_Start()
        Note over TDMA: CLOCK_COLD: no alarm chain (ADR-0015)
        TDMA->>Radio: RadioSetChannel(868.3 MHz) + RadioScan()
    end

    Note over CM0,Radio: Scanning Rx: continuous, CM0+ in Stop2 until a radio IRQ.<br/>Re-armed after every reception while CLOCK_COLD.

    rect rgb(255, 248, 225)
        Note over Net,Radio: Packet 1 — Cold Bootstrap
        Net->>Radio: SyncPayload from C3/C2
        Radio->>MAC: on_rx_done → MAC_OnSyncPacketReceived()
        Note over MAC: CLOCK_COLD → CLOCK_ACQUIRING
        MAC->>CM0: rtc_set() — set RTC calendar
        MAC->>CM0: get_rtc_snapshot() — read back RTC
        MAC->>TDMA: sync_bootstrapped()
        TDMA->>TDMA: BootstrapFromSync()<br/>re-anchor cursor + program Alarm A<br/>(alarm chain starts)
    end

    Note over TDMA,Radio: Next cell: Alarm A fires guard early,<br/>RadioSetRx() until the latest packet start

    rect rgb(255, 248, 225)
        Note over Net,Radio: Packet 2 — Lock Check
        Net->>Radio: SyncPayload
        Radio->>MAC: MAC_OnSyncPacketReceived()
        Note over MAC: error < 8 ms → s_sync_consecutive = 1
    end

    rect rgb(232, 245, 233)
        Note over Net,Radio: Packet 3 — Sync Locked!
        Net->>Radio: SyncPayload
        Radio->>MAC: MAC_OnSyncPacketReceived()
        Note over MAC: error < 8 ms → s_sync_consecutive = 2<br/>CLOCK_ACQUIRING → CLOCK_WARM<br/>SCANNING → SYNCHRONIZED
        MAC->>CM0: sync_locked hook
        CM0->>CM4: IPCC signal: SYNC_LOCKED
    end

    Note over CM0,Radio: Normal TDMA loop continues...

    rect rgb(232, 245, 233)
        Note over Net,Radio: First Beacon Received
        Net->>Radio: BeaconPayload
        Radio->>MAC: MAC_OnBeaconReceived()
        Note over MAC: Compute hop_count, CellEligibilityMask<br/>SYNCHRONIZED → PAIRED
    end

    Note over CM0: Node fully operational — PAIRED + CLOCK_WARM
```

## Timeline Summary

| Step | Event | Clock State | MAC State |
|------|-------|-------------|-----------|
| Boot | `MAC_Init()` | COLD | SCANNING |
| Sync Pkt 1 | RTC set + cursor bootstrap | ACQUIRING | SCANNING |
| Sync Pkt 2 | Error < 8 ms, counter = 1 | ACQUIRING | SCANNING |
| Sync Pkt 3 | Error < 8 ms, counter = 2 | **WARM** | **SYNCHRONIZED** |
| Beacon | Hop count + eligibility mask | WARM | **PAIRED** |

## C3 (Gateway) Difference

C3 skips this entire sequence. It boots directly into `MAC_STATE_ACTIVE` with `CLOCK_WARM` and immediately begins transmitting Sync packets as the SyncAnchor.
