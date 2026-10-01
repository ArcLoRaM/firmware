# Issue #55: a Packet 1 clock set leaves the C2 behind the C3

Purpose: acceptance. Issue: #55.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `tools/bench/scenarios/c2-packet1-set-offset.toml` | The acquisition packets after the set (`SYNC_RX act=good`) read `err` within -1..1 ms; none outside |

## Hardware

| Item | Needed |
|---|---|
| Node 1 flashed as C3, probe `003D003D3234510833353533` | yes |
| Node 2 flashed as C2, probe `003E002E3333511431363730` | yes |

Hands on the bench: none.

## Criteria

- [x] First `SYNC_RX` after a Packet 1 set within +/-1 ms on the bench (was -3): `err=0` on both acquisition packets, two boots.
- [x] C2 stamp - C3 TX start within +/-1 ms right after lock (cross-node, `arclog report` on both nodes): 0, 0, 0, 0 ms on the first four packets, then -1, -1, -1, -1, -2, -2 from drift (-9.7 ppm).
- [x] Host: `Tests/unit/test_rtc_set_plan.c` (the new clock reads the sender's time within one tick for any anchor and edge position, sub-second, advance or delay, across midnight).

## Measurement (2026-10-01, `BENCH_RTC_SET_PROBE`, not committed)

SysTick timing of the old `mac_hook_rtc_set` on the C2, CM0+ at 4 MHz:

| Step | us |
|---|---|
| Hook entry to `HAL_RTC_SetTime` | 760 |
| `HAL_RTC_SetTime` | 672 |
| `HAL_RTC_SetDate` | 579 |
| SHIFTR write / until applied | 323 / 1392 |

Each init-mode exit restarts the calendar at the written second's .000, so everything from the MAC's snapshot to `SetDate`'s exit (about 2 ms, plus the MAC path before the hook) was lost: the 2-3 ms the C2 ran behind.

## Fix

`mac_hook_rtc_set` (`CM0PLUS/SubGHz_Phy/App/subghz_phy_task.c`) no longer loses the write time:

- The snapshot hook keeps its old-clock tick reading (the anchor the MAC's target refers to); `TIMER_IF_GetDayTicks` reads TR/SSR directly in microseconds.
- `RtcSetPlan_Make` (`CM0PLUS/SubGHz_Phy/Logic/rtc_set_plan.h`, host-tested) computes the calendar second and a base SHIFTR first.
- Then the old clock is read exactly on a tick edge, time and date are written in one init-mode session (register writes), and the SHIFTR applies base + the ticks from the anchor to the edge, as an advance or a delay.
- The only constant left is the calendar's stand-still from the edge to its restart, `RTC_SET_LOSS_US = 180` (INITF within 2 RTCCLK + restart 4 RTCCLK, RM0453), well under one tick of error.
- `RTC_SET` logs `adv=` (SHIFTR ticks, + advance, - delay).

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
| 2026-10-01 22:37 | `c2-packet1-set-offset.toml` | `c9fda52` | FAIL (red, expected) | `tools/arclog/runs/20261001T223751Z-c9fda52/` | `SYNC_RX act=good err=-2` at +42.6 s. |
| 2026-10-01 22:40 | flags, `-D BENCH_RTC_SET_PROBE=1` | `c9fda52-d8b4ce7-o7fa961` | measurement | `tools/arclog/runs/20261001T224047Z-c9fda52-d8b4ce7-o7fa961/` | Timings above; `err=-3` on both good packets. |
| 2026-10-01 22:51 | `c2-packet1-set-offset.toml` | `c9fda52-d301e90` | PASS | `tools/arclog/runs/20261001T225118Z-c9fda52-d301e90/` | C2 `RTC_SET ... shift=ok adv=146`; `SYNC_RX act=good err=0`, `err=0`. C3 bench start `adv=10`. |
| 2026-10-01 22:53 | flags: 2 good within -1..1, then 8 `act=t1` | `c9fda52-d301e90` | PASS | `tools/arclog/runs/20261001T225338Z-c9fda52-d301e90/` | Second boot: `adv=147`, `err=0` x2 good, then t1 0, 0, -1, -1, -1, -1, -2, -2 (drift -9.68 ppm). Stamp - TX start 0 on the first four packets. |
