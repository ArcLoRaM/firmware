# Issue #69: every firmware build reports 0 warnings

Purpose: acceptance. Issue: #69 (open, dropped for now 2026-10-02: both warnings are harmless; not fixed).

## Scenarios

| Scenario | Proves or measures |
|---|---|
| `bench build --class C1 --class C2 --class C3 --clean` | 0 warnings, both cores, every class |
| Smoke runs: `bench run --node 1=C3`, `tools/bench/scenarios/smoke-c2.toml`, `bench run --node 2=C1` | The images still boot after the fix |

## Hardware

| Item | Needed |
|---|---|
| Nodes 1 and 2 (smoke runs only) | yes |

Hands on the bench: the CubeMX regeneration (Simon).

## Fix (when picked up)

1. `FLASH_RAM_buffer` unused (`CM0PLUS/Core/Src/sys_app.c`): drop the unused flash interface utility (FLASH_IF) in `ArcLoRaM_Base.ioc` and regenerate; `git diff` shows only the FLASH_IF lines leaving, USER CODE regions unchanged.
2. RWX LOAD segment (both cores): either `-Wl,--no-warn-rwx-segments` in every linker configuration of both `.cproject` files (Release included), or `.RamFunc` in its own segment in both `STM32WL55JCIX_FLASH.ld`. Open choice.

## Criteria

- [ ] `bench build --class C1 --class C2 --class C3 --clean` ends with `0 warning(s)`.
- [ ] A second regeneration from the `.ioc` without changes leaves `git status` clean.
- [ ] Linker-flag option: the flag is in every linker option block of both `.cproject` files. Segment option: each core's `.map` shows `.RamFunc` in its own output section.
- [ ] The three smoke runs pass.

## Runs

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
