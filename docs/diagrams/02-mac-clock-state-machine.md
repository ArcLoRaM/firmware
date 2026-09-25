# MAC + Clock State Machine

> Clock quality drives MAC state transitions. Two coupled FSMs with a three-tier sync dispatch.

## Clock State Machine (ClockState_t)

Tracks RTC synchronisation quality. Drives the MAC state transitions for C1 and C2 nodes.

```mermaid
stateDiagram-v2
    direction LR

    [*] --> CLOCK_COLD : C1/C2 boot

    CLOCK_COLD --> CLOCK_ACQUIRING : 1st Sync received — rtc_set + bootstrap

    CLOCK_ACQUIRING --> CLOCK_WARM : 2 consecutive good packets (error &lt; 8 ms)

    CLOCK_WARM --> CLOCK_COLD : Tier 3 — drift ≥ 100 ms — sync lost
    CLOCK_WARM --> CLOCK_COLD : Sync Silence Timeout — no sync for 15 min
    CLOCK_ACQUIRING --> CLOCK_COLD : Sync Silence Timeout — no sync for 15 min

    note right of CLOCK_COLD
        No sync received.
        Scanning Rx: continuous on the discovery
        channel, no TDMA alarm chain (ADR-0015).
    end note

    note left of CLOCK_ACQUIRING
        Counts consecutive good packets.
        Good (&lt; 8 ms) increments counter.
        Bad (≥ 8 ms) resets to 0.
    end note

    note right of CLOCK_WARM
        Three-tier dispatch per Sync packet.
        Tier 1 (&lt; 8 ms): participate, store epoch.
        Tier 2 (8–99 ms): SSR fine-tune only.
        Tier 3 (≥ 100 ms): full re-anchor, sync lost.
    end note
```

## MAC State Machine (MacState_t)

Operational state driven by Clock transitions and Beacon reception.

```mermaid
stateDiagram-v2
    direction LR

    state "C1 / C2 Path" as c1c2 {
        [*] --> SCANNING : boot default

        SCANNING --> SYNCHRONIZED : CLOCK_WARM reached
        SYNCHRONIZED --> PAIRED : 1st Beacon received

        PAIRED --> SCANNING : sync lost (drift ≥ 100 ms)
        SYNCHRONIZED --> SCANNING : sync lost (drift ≥ 100 ms)
    }

    state "C3 Path (Gateway)" as c3 {
        [*] --> ACTIVE : boot default
        note right of ACTIVE
            SyncAnchor.
            Always CLOCK_WARM.
            No Sync acquisition.
            Never leaves ACTIVE.
        end note
    }
```

## How Clock Drives MAC

```mermaid
flowchart LR
    subgraph Clock["ClockState_t"]
        COLD["CLOCK_COLD"]
        ACQ["CLOCK_ACQUIRING"]
        WARM["CLOCK_WARM"]
        COLD --> |"1st Sync pkt"| ACQ
        ACQ --> |"2 good pkts"| WARM
        WARM --> |"drift ≥ 100 ms"| COLD
        WARM --> |"no sync 15 min"| COLD
        ACQ --> |"no sync 15 min"| COLD
    end

    subgraph MAC["MacState_t"]
        SCAN["SCANNING"]
        SYNCD["SYNCHRONIZED"]
        PAIR["PAIRED"]
        SCAN --> |"on CLOCK_WARM"| SYNCD
        SYNCD --> |"1st Beacon"| PAIR
        PAIR -.-> |"on sync lost"| SCAN
    end

    WARM -- "triggers" --> SYNCD
    COLD -- "triggers" --> SCAN
```

## Three-Tier Sync Dispatch (CLOCK_WARM)

When a node is fully synchronised, each incoming Sync packet is classified by clock error:

| Tier | Error Range | Action | Effect |
|------|-------------|--------|--------|
| **1** | < 8 ms | Participate | Store epoch for relay (C2 sets `s_epoch_received_this_phase = true`) |
| **2** | 8–99 ms | SSR correction | `rtc_align_subsecond()` — fine-tune without relay |
| **3** | ≥ 100 ms | Full re-anchor | `rtc_set()` → `trigger_sync_lost()` → back to SCANNING |

## Key Thresholds

| Constant | Value | Purpose |
|----------|-------|---------|
| `SYNC_PARTICIPATE_THRESHOLD_MS` | 8 ms (0.25·T_S) | Below this: clock is good enough to participate |
| `SYNC_RESYNC_THRESHOLD_MS` | 100 ms (3·T_S) | Above this: clock has drifted too far, full re-sync |
| `SYNC_LOCK_THRESHOLD_MS` | 16 ms (0.5·T_S) | CT drift budget for pure CT model (deferred); not used in current relay model |
| `SYNC_SILENCE_TIMEOUT_MS` | 15 min (provisioned) | Wall-clock timeout with no Sync packet received → `CLOCK_COLD`. Decoupled from TDMA table. See ADR-0013. |
