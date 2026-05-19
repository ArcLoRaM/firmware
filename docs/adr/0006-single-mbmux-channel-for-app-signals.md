# ADR-0006: Single MbMux Notification Channel for CM0+ → CM4 Application Signals

## Status
Accepted

## Context
CM0+ needs to signal CM4 asynchronously for three event types: `RX_READY`,
`TX_NO_ACK`, and `SYNC_LOCKED`. Three options were considered:

- **Option A**: Raw IPCC channels, bypassing MbMux entirely.
- **Option B**: One MbMux `NOTIF_ACK` feature per signal type (three channels).
- **Option C**: One MbMux `NOTIF_ACK` feature shared by all three signals,
  distinguished by `MsgId`.

Raw IPCC (A) was eliminated first: it requires manual IPCC channel number
management that risks conflicting with MbMux's allocations, and introduces two
different inter-core communication patterns in the same codebase — a maintenance
burden with no meaningful performance gain at the event rates involved.

Between B and C: the STM32WL55 IPCC peripheral has 6 physical channels. MbMux
already consumes 3–4 for system, radio, and trace features. Adding three
dedicated app signal channels risks exhausting the budget and leaves no room for
future signals.

The IPCC channel latch stays set until CM4 ACKs the notification. A second
`MBMUX_NotificationSnd` on the same channel before the ACK returns `-1`.
With a single shared channel, two signals firing in quick succession could
result in the second being dropped.

## Decision
Option C. One `FEAT_INFO_APP_ID` feature, all three signal types multiplexed
via `MsgId`. To prevent silent drops when the channel is busy: CM0+ checks the
return value of `NotificationSnd`. On `-1`, it writes the pending `MsgId` to a
one-byte shared memory flag. CM0+'s ACK callback re-fires the notification if
the flag is set. This guarantees delivery without consuming additional channels.

## Consequences
The pending signal flag adds one shared memory byte and one re-fire code path
in CM0+'s ACK callback. Signal ordering is not guaranteed when a re-fire occurs
— CM4 receives the re-fired event after the first event's ACK, not at the
original fire time. This is acceptable given the loose timing requirement on all
CM0+ → CM4 signals: missing one radio opportunity is explicitly tolerated by
design.
