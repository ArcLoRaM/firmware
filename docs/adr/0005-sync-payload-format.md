# ADR-0005: Sync Payload Uses Raw BCD + SSR, Not a Flat Integer

## Status
Accepted

## Context
The `Sync` Phase transmits a packet whose payload carries the Frame Epoch — the
absolute RTC time at the start of the current frame. Receivers use this to set
their RTC on cold start and to correct clock drift on every subsequent frame.

The project uses `RTC_BINARY_NONE` mode: the STM32 RTC stores time as a BCD
calendar (hours, minutes, seconds, date) plus a raw SubSeconds SSR register that
counts **down** from `PREDIV_S` to 0. Any elapsed-ms computation requires:

    ss_ms = (PREDIV_S − SubSeconds) × 1000 / (PREDIV_S + 1)

Two payload formats were considered:

**Option A — Raw BCD + SSR** (chosen): transmit the fields exactly as
`RTC_TimeTypeDef` and `RTC_DateTypeDef` return them. The receiver calls
`HAL_RTC_SetTime(payload, RTC_FORMAT_BCD)` and
`HAL_RTC_SetDate(payload, RTC_FORMAT_BCD)` directly — no write-path conversion.

**Option B — Compact integer**: transmit `uint32_t` seconds-since-2000 +
`uint16_t` milliseconds. Smaller on wire, but requires BCD↔integer conversion
on both the transmit path (reading RTC) and the receive path (writing RTC).

The `Sync` Phase also supports multi-hop relaying: the SyncAnchor transmits in
slot 0; relay nodes forward the packet in subsequent slots (all uniform duration).
The receiver needs to know which slot it received from to compute the correct
preamble-arrival offset.

## Decision
The `SyncPayload` struct carries raw BCD calendar fields, raw SSR, and a
`sync_slot_index` field (which slot within the Sync Phase transmitted this packet).
Relay nodes forward the struct verbatim except for incrementing `sync_slot_index`.

Rationale:
1. **Write-path conversion eliminated.** `RTC_BINARY_NONE` forces the SSR formula
   for elapsed-ms arithmetic regardless of payload format; using BCD avoids a
   *second* conversion on the RTC write path.
2. **Relay-strategy-agnostic.** `sync_slot_index` lets the receiver compute
   `expected_offset_ms` without knowing whether relays use concurrent transmission
   or a random sub-slot strategy — the choice is deferred.
3. **No new integer types.** The BCD fields map 1:1 to HAL struct members,
   eliminating any risk of unit confusion (seconds vs milliseconds vs ticks).

## Consequences
The `SyncPayload` struct is 11 bytes. Elapsed-ms arithmetic still requires the
SSR formula — that cost is unavoidable under `RTC_BINARY_NONE` and is isolated to
`rtc_to_ms()` in `sync.c`. The cold-start set loses sub-second precision
(`HAL_RTC_SetTime` does not accept SubSeconds on write); this is accepted as a
one-frame imprecision corrected on the second sync packet.
