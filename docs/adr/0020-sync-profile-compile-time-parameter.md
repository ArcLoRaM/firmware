# ADR-0020: The Sync profile is a compile-time parameter

## Status
Accepted for the mechanism and the DEV profile. The PROD numbers are provisional until #45 derives them.

## Context
Sync is protocol overhead and is kept as low as the clocks allow: the longest Sync period the drift bound permits (#45).
A long period has a cost for everything that is not about sync: a cold node needs three packets to lock, one per phase, and the rate estimator needs eight samples, so at the period of the product a node takes tens of minutes to lock and over an hour to learn its rate.
Work that assumes synchronised clocks (an Uplink test) must not wait for that.

The bring-up table that was the default (a Sync phase every 30 s, three packets per node per phase) is no answer to this: it is 9.9 % duty cycle against the 1 % band, the compliance credit is gone after 6.1 min and the nodes are denied from then on (measured on the bench, reproduced on the host).
A development session on it would lose its synchronisation in the middle.

## Decision
One compile-time parameter, `SYNC_PROFILE` (`Common/Protocol/sync_profile.h`), picks the Sync schedule through the single TDMA table accessor (ADR-0003):

| Profile | Sync phase | Packets per node per phase | Duty cycle of a node | Silence timeout | Use |
|---|---|---|---|---|---|
| `SYNC_PROFILE_DEV` (the default) | 200 s | 1 | 0.496 % | 15 min | development: frequent Sync, inside the budget |
| `SYNC_PROFILE_PROD` | 540 s (provisional) | 1 | 0.18 % | 27 min (3 periods) | the product; #45 replaces the numbers |
| `SYNC_PROFILE_BRINGUP` | 30 s | 3 | 9.9 % | 15 min | the host tests, which were written against it |

- **One transmission is one repetition.** `SYNC_TX_BUDGET` is 1 in the DEV and PROD profiles: the Sync airtime is one packet per node per phase.
- **DEV is sized to 0.5 % of the band**, one packet of 991 ms per 198.2 s at the least, rounded to 200 s, so the Sync overhead leaves the other half of the band to the data phases.
- **The PROD period** follows the drift bound of #45 with one lost packet tolerated: `(k + 1) x period <= (5 ms - 3 ms - margin) / residual`, 9.2 min at the 0.72 ppm ceiling of the NUCLEO Clock, rounded down to 9 min.
  It is provisional: the relative residual between neighbours (which can halve it), the Production Clock (to be measured on its hardware) and the correction threshold of #36 all move it.
- **The silence timeout belongs to the profile.** One lost packet (k = 1) must not drop a node to `CLOCK_COLD`: it outlasts two periods, three for PROD (ADR-0013).
- **Each constant stays overridable on its own** (`-D SYNC_TX_BUDGET=1u`), so a scenario can still vary one value, and the host tests pin `BRINGUP`.
- **The profile is logged** in the CM0+ `BOOT` line (`sync=`), so a trace says which schedule it ran.
- The default is DEV while PROD is provisional; it becomes PROD when #45 lands its table.

## Considered Options

- **A run-time switch (a command from the CM4 or the network).** Rejected: the table is compile-time with one accessor (ADR-0003), and a product node must not be able to change its Sync overhead.
- **Keep the bring-up table as the development schedule.** Rejected: it starves itself after 6.1 min.
- **A dense boot burst, then the product period.** Not rejected, not built: it would shorten the cold start of a node that boots with the C3, not of one reset later. Open.

## Consequences

- A cold node reaches `CLOCK_WARM` in about 10 min on DEV (three phases) and 27 min on PROD, and its rate estimate is valid after about 33 min on DEV and 72 min on PROD (eight samples).
  The factory baseline (#23, ADR-0019) is what covers the rate on PROD until then.
- The Sync airtime shares the 1 % band with the data phases: DEV leaves about half of it.
- The MAC's `SYNC_TX_BUDGET` and `SYNC_SILENCE_TIMEOUT_MS` are defined by the profile; the MAC header no longer carries its own defaults.
