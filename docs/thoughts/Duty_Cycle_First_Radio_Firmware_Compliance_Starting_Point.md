# Duty-Cycle-First Radio Firmware Compliance — Architecture Starting Point

## Purpose

This document provides a firmware-oriented starting point for developing a compliant
custom sub-GHz wireless protocol stack.

The first target regulatory model is **ETSI-style duty-cycle enforcement in the
868 MHz band**, where transmission time is limited per sub-band. This is the
primary compliance phenomenon that the initial firmware architecture should
support.

However, the firmware should be structured so that duty-cycle enforcement is not
hard-coded into the MAC layer itself. The MAC should remain responsible for
protocol behaviour, while a separate compliance component decides whether a
transmission is legally allowed at a given moment.

The goal of this document is not to list every regulatory specification in every
country. Instead, it answers the practical firmware question:

> What do I need to know before developing a compliant radio firmware, starting
> from ETSI duty-cycle enforcement, without blocking future regional adaptation?

---

## 1. Start From the Real Initial Constraint: ETSI Duty Cycle

For the initial target, the important regulatory constraint is simple in principle:

```text
In a given ETSI sub-band, the device may only occupy the channel for a limited
fraction of time over a defined observation period.
```

For firmware, this means the system must track:

```text
which sub-band a transmission belongs to
how much airtime the device has already consumed in that sub-band
how much airtime budget remains
when enough time has passed to transmit again
```

This naturally leads to a duty-cycle accounting mechanism.

A typical duty-cycle-first firmware component answers:

```text
Can I transmit now on this frequency for this expected airtime?
```

and then, after transmission:

```text
How much actual airtime was consumed?
```

This is the correct initial focus if the protocol is primarily targeting ETSI
868 MHz duty-cycle-restricted operation.

---

## 2. The Important Architectural Point: Do Not Put Duty Cycle Inside the MAC

Even if duty cycle is the first and most important compliance mechanism, it should
not become an implicit assumption inside the MAC.

The MAC should not directly manage regulatory counters such as:

```text
sub-band airtime credits
duty-cycle replenishment
legal wait time
regulatory observation window
```

Instead, the MAC should treat regulatory access as an external permission gate.

A clean separation is:

```text
MAC layer
    owns protocol behaviour

Compliance layer
    owns regulatory transmit permission

Radio HAL
    owns physical radio operations
```

The MAC asks:

```text
I want to transmit this packet on this frequency with this expected airtime.
Is that allowed now?
```

The compliance layer answers:

```text
yes
no, wait this long
no, this channel is not legal
no, this packet is too long
no, this power is not allowed
```

This separation matters because it lets the first implementation be duty-cycle
based without forcing all future regions to behave like ETSI.

---

## 3. You Do Not Need to Fundamentally Rework the MAC for Every Region

A well-structured MAC should not need to be rewritten when moving from one
regional access mechanism to another.

The following MAC responsibilities can remain stable across regions:

```text
packet queueing
application scheduling
routing
neighbour discovery
ACK handling
retry policy
association or join logic
priority handling
fragmentation
link metrics
```

These are protocol-level behaviours. They should not depend directly on whether
the regulatory reason for waiting is:

```text
ETSI duty-cycle exhaustion
LBT/CCA failure
minimum off-time
maximum transmission duration
FHSS dwell-time limit
illegal channel
configured power too high
```

From the MAC perspective, these are all forms of:

```text
transmission is not allowed now
```

Therefore, the MAC does not need to be region-specific if it interacts with
regulation through a generic transmit-permission interface.

---

## 4. What May Need to Be Flexible: Backoff and Rescheduling

The MAC does not need to be fundamentally reworked, but its scheduler should not
assume that every restriction behaves like ETSI duty cycle.

For ETSI duty cycle, the wait time is mostly deterministic:

```text
airtime was consumed
credits recover over time
next legal transmission time can be calculated
```

For Listen Before Talk / CCA, the wait may be dynamic:

```text
channel is sensed
channel is busy
MAC waits or backs off
MAC retries CCA later
```

For frequency-hopping systems, the issue may be channel usage rather than total
sub-band airtime:

```text
one hopping channel has reached an occupancy limit
another channel may still be available
```

For regions with maximum transmission duration, the issue may be packet size or
data rate:

```text
this packet is too long for the selected region/channel
MAC must shorten, fragment, change data rate, or reject it
```

So the MAC scheduler should be able to handle richer access outcomes than a simple
duty-cycle yes/no result.

Useful generic outcomes include:

```text
GRANTED
RESTRICTED_WAIT(wait_ms)
CHANNEL_BUSY(backoff_hint_ms)
ILLEGAL_CHANNEL
TX_TOO_LONG
POWER_TOO_HIGH
TRY_OTHER_CHANNEL
```

The first implementation may only use:

```text
GRANTED
RESTRICTED_WAIT(wait_ms)
ILLEGAL_CHANNEL
```

That is fine. The important point is that the interface should not prevent the
other outcomes from being introduced later.

---

## 5. Duty-Cycle-First Does Not Mean Duty-Cycle-Only Forever

The initial compliance component can be designed around ETSI duty-cycle accounting.
That is a reasonable first step.

However, the firmware should describe this as the **first enforcement strategy**,
not as the only possible form of regulatory access control.

A useful mental model is:

```text
Spectrum Access Compliance Component
    └── first implemented strategy: ETSI duty-cycle accounting
```

This does not mean the project must implement LBT, FHSS, dwell-time, or off-time
from day one.

It only means the code structure should leave room for them.

The initial implementation can stay narrow:

```text
region profile
sub-band table
duty-cycle limits
airtime accounting
pre-TX permission check
post-TX airtime report
```

while the architecture remains open enough to later support:

```text
CCA/LBT
minimum off-time
maximum TX duration
frequency-hopping occupancy
regional power constraints
```

---

## 6. What the Compliance Layer Should Know for ETSI Duty Cycle

For the ETSI duty-cycle-first implementation, the compliance layer should know:

```text
active region profile
allowed frequency ranges
sub-band boundaries
duty-cycle limit per sub-band
observation window
maximum allowed transmit power per sub-band
current airtime state per sub-band
time of last state update
expected time-on-air for proposed transmission
actual time-on-air after transmission
```

The important regulatory unit is the **sub-band**.

A transmission must be mapped to the sub-band that governs its frequency. The
duty-cycle budget must then be checked for that sub-band only.

The compliance layer should answer:

```text
Is the frequency inside an allowed sub-band?
Does the sub-band have enough duty-cycle budget?
How long must the MAC wait if it does not?
What airtime should be deducted after transmission?
```

This keeps ETSI compliance precise without forcing the MAC to understand the
details of sub-band accounting.

---

## 7. What the MAC Should Know

The MAC should know as little regulatory detail as possible.

The MAC should know:

```text
I have a packet to send.
I have one or more candidate channels.
I can estimate time-on-air.
I can ask whether transmission is allowed.
I can reschedule if transmission is restricted.
I can report when transmission is complete.
```

The MAC should not need to know:

```text
the exact duty-cycle percentage
the sub-band accounting formula
the observation window
the airtime credit state
the regulatory wait-time calculation
```

This keeps the MAC clean and portable.

The MAC owns policy decisions such as:

```text
Which packet should be sent first?
Should I retry?
Should I try another channel?
Should I delay?
Should I drop the packet?
Should I fragment?
Should I reduce payload size?
Should I change data rate?
```

The compliance layer owns legality decisions such as:

```text
Is this frequency allowed?
Is this sub-band available now?
Has the duty-cycle budget been exhausted?
Is the expected transmission too long?
Is the configured power allowed?
```

---

## 8. Region Profiles: Keep Regulation as Data

A practical way to avoid future MAC rewrites is to represent regulatory information
as region-profile data rather than hard-coded MAC logic.

For the first ETSI duty-cycle implementation, a profile may contain:

```text
region_id
frequency_min
frequency_max
sub_bands[]
observation_window_ms
duty_cycle_limit_per_sub_band
max_tx_power_per_sub_band
```

Each sub-band may contain:

```text
freq_min_hz
freq_max_hz
duty_cycle_limit
max_tx_power_dbm_or_eirp
```

Runtime state should be separate from static regulation data:

```text
airtime_budget_remaining
last_update_time
next_available_time
```

This separation helps auditability:

```text
regulatory requirement → profile field → enforcement path → test case
```

It also makes later regional profiles easier to add without touching the MAC.

---

## 9. What Changes When Adding Other Regions Later?

If the MAC is properly separated from the compliance layer, adding other regions
should mostly affect the compliance component and region-profile definitions.

### ETSI duty-cycle region

Typical firmware behaviour:

```text
track airtime per sub-band
calculate remaining duty-cycle budget
return wait time when budget is exhausted
```

MAC impact:

```text
reschedule packet after wait_ms
possibly try another legal channel/sub-band
```

### LBT / CCA region

Typical firmware behaviour:

```text
perform or request channel-clear assessment
return GRANTED if clear
return CHANNEL_BUSY if occupied
provide a backoff hint if appropriate
```

MAC impact:

```text
retry later
possibly try another channel
preserve normal packet/retry logic
```

### Frequency-hopping region

Typical firmware behaviour:

```text
track or validate hopping-channel usage
enforce dwell-time or occupancy limits where required
ensure channel set rules are respected
```

MAC impact:

```text
use a compliant hopping/channel-selection policy
avoid retrying indefinitely on one restricted channel
```

### Region with maximum transmission duration

Typical firmware behaviour:

```text
reject proposed transmissions that exceed maximum allowed duration
```

MAC impact:

```text
fragment
reduce payload
change data rate
drop or defer packet
```

The MAC may need to support these behaviours, but that is different from rewriting
the MAC. The MAC needs flexible scheduling and retry decisions, while the compliance
layer handles the regional legality checks.

---

## 10. Backoff: Regulatory Wait vs MAC Backoff

It is useful to distinguish between two different concepts:

```text
regulatory wait
MAC backoff
```

A regulatory wait is imposed by compliance:

```text
You cannot legally transmit before this time.
```

A MAC backoff is a protocol decision:

```text
You may choose to wait to avoid collisions, congestion, or repeated failures.
```

In ETSI duty-cycle enforcement, the compliance layer can often calculate a precise
regulatory wait:

```text
wait_ms until enough duty-cycle budget is available
```

In LBT/CCA operation, the compliance layer may report:

```text
channel busy
```

and the MAC may apply a randomized or policy-driven backoff before trying again.

Do not mix these two concepts.

The compliance layer should provide legal constraints.  
The MAC should decide scheduling policy within those constraints.

---

## 11. What You Need to Know Before Starting Development

Before starting the firmware implementation, clarify the following.

### For the initial ETSI duty-cycle implementation

You need to know:

```text
Which ETSI band and sub-band are used?
What are the exact sub-band frequency boundaries?
What duty-cycle limit applies to each sub-band?
What observation window is used?
What is the maximum allowed transmit power or EIRP?
How is time-on-air calculated?
Will actual airtime be measured by the radio driver?
Can the MAC use multiple sub-bands or only one?
What should happen when duty-cycle budget is exhausted?
Should the MAC wait, try another channel, drop, or downgrade traffic?
```

### For future regional portability

You should also avoid assumptions that would block:

```text
CCA/LBT before transmission
randomized or policy-driven backoff
minimum off-time after transmission
maximum single-transmission duration
frequency-hopping channel-use constraints
power limits varying by channel or sub-band
region-profile switching
```

You do not need to implement all of these now.

You only need to avoid an architecture that makes them impossible without rewriting
the MAC.

---

## 12. Recommended Architecture Framing

The project can still be described as duty-cycle-first.

A good description is:

> The initial implementation enforces ETSI-style per-sub-band duty-cycle
> restrictions for 868 MHz operation. The enforcement logic is separated from the
> MAC through a generic transmission-permission interface, allowing future regional
> access mechanisms such as LBT, CCA, dwell-time limits, or off-time rules to be
> added without redesigning the MAC.

This is more accurate than saying:

```text
The MAC enforces duty cycle.
```

or:

```text
The compliance engine only handles duty cycle because duty cycle is the regulatory
requirement.
```

A better naming convention is:

```text
Spectrum Access Compliance Engine
```

with the first implemented strategy:

```text
ETSI Duty-Cycle Strategy
```

This keeps the current scope realistic while preventing the architecture from
becoming region-locked.

---

## 13. Practical Initial Scope

For the first implementation, it is reasonable to include only:

```text
RegionProfile
SubBand table
Duty-cycle accounting
Pre-TX permission request
Post-TX airtime report
Wait-time calculation
Illegal-channel rejection
Basic power-limit validation
```

It is reasonable to exclude, for now:

```text
LBT/CCA
FHSS dwell-time enforcement
minimum off-time
adaptive frequency agility
region roaming
complex multi-region certification logic
```

But these exclusions should be documented as:

```text
not implemented in the first strategy
```

rather than:

```text
not relevant to compliance
```

That distinction is important.

---

## 14. Minimal Conceptual Interface

The exact API can be designed later, but the conceptual interaction should be:

```text
Before transmission:
    MAC requests permission to transmit.

After transmission:
    radio driver or MAC reports actual airtime consumed.
```

For the ETSI duty-cycle-first implementation:

```text
RequestTx(freq_hz, expected_toa_ms, tx_power)
    → GRANTED
    → RESTRICTED_WAIT(wait_ms)
    → ILLEGAL_CHANNEL
    → POWER_TOO_HIGH
```

After transmission:

```text
ReportTxDone(freq_hz, actual_toa_ms)
```

This interface is enough for duty-cycle enforcement but can later be extended for:

```text
CHANNEL_BUSY
TX_TOO_LONG
TRY_OTHER_CHANNEL
CCA_REQUIRED
```

The key point is not the exact function names. The key point is the direction of
dependency:

```text
MAC depends on compliance permission.
Compliance does not depend on MAC internals.
```

---

## 15. Final Answer

If your protocol is primarily targeting ETSI 868 MHz duty-cycle-restricted
operation, you can absolutely start with a duty-cycle compliance engine.

You do not need to fundamentally redesign the whole MAC for future regions, as long
as you avoid putting duty-cycle assumptions directly inside the MAC scheduler.

The correct starting point is:

```text
duty-cycle-first implementation
generic transmit-permission interface
region-profile-driven compliance data
MAC-level rescheduling/backoff policy
clean separation between regulatory wait and MAC backoff
```

This allows your first firmware version to be simple and focused on ETSI duty-cycle
compliance, while preserving the option to support other regional access mechanisms
later without rewriting the MAC from scratch.
