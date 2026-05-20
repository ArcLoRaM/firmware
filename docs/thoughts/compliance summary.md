# Duty Cycle Compliance Engine — Architecture Document

## Purpose

This document describes the architecture of a MAC-agnostic regulatory duty cycle
enforcement engine intended for use in a custom sub-GHz radio protocol stack. The
engine enforces spectrum access rules mandated by regional radio regulations
(ETSI EN 300 220-2, FCC Part 15, ARIB STD-T108, and equivalents). It is designed
to support third-party certification (CE/RED, FCC ID, TELEC) and source-level
compliance audits. The engine is entirely decoupled from the custom MAC layer — the
MAC calls the engine; the engine knows nothing about the protocol above it.

---

## Regulatory Model

Radio duty cycle restrictions are **per-sub-band** requirements defined by national
or regional regulatory bodies. The constraints differ significantly across domains:

| Domain       | Standard            | Mechanism              | Typical Limit     |
|--------------|---------------------|------------------------|-------------------|
| EU 868 MHz   | ETSI EN 300 220-2   | Time-based duty cycle  | 0.1 % – 10 %      |
| EU 433 MHz   | ETSI EN 300 220-2   | Time-based duty cycle  | 10 %              |
| US 915 MHz   | FCC Part 15.247     | Frequency hopping / EIRP | None (time-based) |
| Japan 920 MHz| ARIB STD-T108       | Duty cycle + LBT       | 1 % + CCA         |
| Korea 920 MHz| KCC                 | LBT only               | CCA required      |

**Key principle:** The per-sub-band limit is the sole regulatory requirement.
There is no cross-band aggregated cap mandated by any of the above standards.
Cross-band aggregation is a protocol-level fairness mechanism, not a regulatory one,
and is explicitly excluded from this engine.

The enforcement mechanism is not uniform across domains. The engine must dispatch
to the correct strategy per region via the `EnforcementStrategy` abstraction (see below).

---

## RegionProfile

A `RegionProfile` is a self-contained, loadable descriptor for one geographic
regulatory domain. The engine holds exactly one active `RegionProfile` at runtime,
selected at device startup (or updated when the device changes regulatory domain).

### Structure

```
RegionProfile
├── region_id         : enum identifier (EU868, US915, AS923, AU915, IN865 …)
├── observation_window_ms : credit replenishment window (typically 1 800 000 ms)
├── bands[]           : array of Band descriptors (see Band below)
└── enforcement       : EnforcementStrategy (TIME_CREDIT | LBT | NONE)
```

### Band descriptor

Each entry in `bands[]` describes one regulatory sub-band:

```
Band
├── freq_min_hz       : lower edge of sub-band
├── freq_max_hz       : upper edge of sub-band
├── duty_cycle        : quota denominator (100 = 1 %, 1000 = 0.1 %, 10 = 10 %)
├── max_tx_power_dbm  : regulatory EIRP ceiling for this sub-band
├── time_credits_ms   : current available transmission time (runtime state)
├── max_credits_ms    : ceiling, replenished over observation_window_ms (runtime state)
├── last_update_ms    : timestamp of last credit synchronisation (runtime state)
└── ready             : boolean gate, set by the engine before each TX query
```

The static fields (`freq_min_hz`, `freq_max_hz`, `duty_cycle`, `max_tx_power_dbm`)
are compile-time constants per region. The runtime state fields are initialised to
`max_credits_ms` at boot and mutated by the engine on every TX event.

**Channel-to-band mapping** is part of the RegionProfile: each channel frequency
is looked up against `bands[]` ranges to determine which Band governs it.

---

## EnforcementStrategy

The engine supports three enforcement strategies, selected per RegionProfile.
Only one strategy is active per region. Strategies are not mixed within a single
RegionProfile.

### TIME_CREDIT (EU, AS923, AU915 sub-band variants)

Per-band time-credit accounting:

1. **Credit replenishment**: On each pre-TX query, elapsed time since
   `last_update_ms` is used to add credits:
   `credits_earned = elapsed_ms / duty_cycle`
   Credits are capped at `max_credits_ms`.

2. **Cost calculation**: The cost of the proposed transmission is:
   `credit_cost = expected_time_on_air_ms × duty_cycle`

3. **Gate decision**: Band is `ready` if `time_credits_ms ≥ credit_cost`.
   If no band covering the requested channel is ready, the engine returns
   `RESTRICTED` and the minimum wait time across all bands.

4. **Post-TX deduction**: After transmission, the engine deducts actual air time:
   `time_credits_ms -= actual_time_on_air_ms × duty_cycle`

### LBT — Listen Before Talk (Japan ARIB, Korea KCC)

The engine delegates the channel-clear assessment to the radio HAL before
granting a TX slot. The radio performs a CCA (Clear Channel Assessment) of
configurable duration. If the channel is busy, the engine returns `RESTRICTED`
with a back-off delay. LBT may be combined with `TIME_CREDIT` (ARIB requires both).

### NONE (US FCC Part 15, frequency-hopping regimes)

The engine always returns `GRANTED`. Channel selection and spectrum spreading are
the responsibility of the MAC layer (e.g., frequency hopping). The engine imposes
no time-based restriction. This strategy must only be selected when the regulatory
domain genuinely has no time-based duty cycle requirement.

---

## Interface Contract

The engine exposes exactly two mandatory hooks to the MAC layer. These are the
**only** points of contact. The MAC must not access band state directly.

### `RequestChannel(freq_hz, expected_toa_ms) → Result`

Called by the MAC **before** any transmission attempt.

- The engine identifies the governing Band from `freq_hz`.
- Applies the active EnforcementStrategy.
- Returns one of:
  - `GRANTED` — transmission may proceed immediately.
  - `RESTRICTED { wait_ms }` — transmission is not permitted; MAC must delay
    at least `wait_ms` before retrying.
  - `BAND_UNKNOWN` — the requested frequency does not fall within any declared
    sub-band of the active RegionProfile; MAC must not transmit.

### `ReportTxDone(freq_hz, actual_toa_ms)`

Called by the MAC **immediately after** the radio completes transmission.

- `actual_toa_ms` is measured by the radio driver (not estimated).
- The engine deducts credits from the governing Band.
- No return value; fire-and-forget from the MAC's perspective.

### Extension points (not yet designed)

The interface is intentionally minimal. Future hooks may include:

- `ReportTxAborted(freq_hz)` — TX was cancelled after `RequestChannel` granted;
  no air time consumed.
- `NotifyRegionChange(region_id)` — swap the active RegionProfile at runtime
  (roaming, regulatory domain update).
- `QueryWaitTime(freq_hz, expected_toa_ms) → wait_ms` — non-blocking lookahead
  without committing to a TX.

---

## Traceability

The following table maps each regulatory requirement to its implementation artefact.
This table is the primary evidence for a source-level compliance audit.

| Regulatory Requirement | Implementation Artefact |
|------------------------|------------------------|
| Per-sub-band duty cycle limit | `Band.duty_cycle` in `RegionProfile.bands[]` |
| Sub-band frequency boundaries | `Band.freq_min_hz / freq_max_hz` |
| Maximum TX power per sub-band | `Band.max_tx_power_dbm` |
| Duty cycle accounting (time-credit) | `RequestChannel` + `ReportTxDone` (TIME_CREDIT strategy) |
| Credit replenishment over observation window | `RegionProfile.observation_window_ms` |
| LBT (ARIB / KCC) | `EnforcementStrategy.LBT` path in `RequestChannel` |
| No restriction (FCC hopping) | `EnforcementStrategy.NONE` |
| Region-specific rule set | `RegionProfile` loaded at startup, one active at a time |

---

## Multiregion Deployment

The engine holds one active `RegionProfile` at a time. The set of available
profiles is compiled into the firmware; only the profiles for the target
deployment regions need to be included, reducing code size.

At startup, the active profile is selected by configuration (e.g., a provisioned
region identifier). If the device is capable of roaming across regulatory domains,
a `NotifyRegionChange` hook (extension point above) allows hot-swapping the profile.

All `Band` runtime state (credits, timestamps) is reset to full on a profile swap.

---

## What This Engine Does Not Cover

- **Channel selection** — choosing which specific frequency to use within a band
  is the MAC's responsibility. The engine only gates and accounts; it does not select.
- **Adaptive data rate / spreading factor** — not a regulatory concern handled here.
- **Network-level fairness** — cross-band aggregated caps, back-off policies, and
  retransmission scheduling are MAC-layer policies, not engine responsibilities.
- **Join / association phase backoff** — the engine applies flat per-band enforcement
  uniformly across all protocol phases. Phase-specific tightening is not required
  when the association procedure is coordinated and time-bounded.
