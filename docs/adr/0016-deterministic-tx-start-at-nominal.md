# Deterministic Tx start: every packet on air at its slot's nominal start

Every scheduled packet starts on air exactly at its slot's nominal start, for every phase, node class and hop.
The sender makes it so: a Tx slot is woken `TX_LEAD_MS` (20 ms) early, all variable work runs first (including the radio's wake-up into standby with its oscillator running), and the node then busy-waits for the fire instant, the nominal start minus the radio's Tx ramp, and calls `Radio.Send`.
Receivers are unchanged: they already expect every packet at the nominal start.
The Sync Phase Epoch C3 sends is the phase's nominal start from the schedule, not an RTC reading.

The bench (C3-C2, 2026-09-26) showed why.
A Tx node woke at the nominal start and did its work before `Radio.Send`, so its packet went on air 9 ms late in cells 1-2 and 13 ms late in cell 0 (the extra phase-entry work, and C3's epoch snapshot taken 1 ms after the nominal start).
Receivers read that lateness as clock error: the cell-0 step used half of the 8 ms Tier-1 budget, left acquisition a ~2 ms margin, and each hop inherits the lateness of the one before (C2 ~12 ms behind C3, a C1 behind it ~21 ms), which concurrent transmission must then absorb.

## Considered Options

**Fixed delay after the nominal start (`TX_START_DELAY_MS`, issue #44's first proposal).**
Transmit at `nominal + 20 ms`, and have receivers add the constant to every expected arrival (Packet 1 set, acquisition, the three tiers), shift their early wake by it, and fit it into every slot (`slot_active_ms >= delay + ramp + ToA`).
Rejected: it achieves the same determinism by redefining "nominal" in every receiver formula, makes a sender's software budget a protocol constant every node must share, and takes 20 ms out of every slot.
With the lead, the constant is sender-local, the receiving side and the domain rule "the Tx node transmits at nominal time" stay as they were, and any future timing sample from a scheduled packet (issue #49) expects its start at the nominal start with nothing to subtract.

**Keep `Radio.Send` from sleep and subtract a measured ramp (~4 ms).**
Rejected: the radio's wake-up and TCXO start-up then sit between the wait and the air, one more variable term.
Preparing the radio first leaves a fixed SPI sequence plus PLL lock and PA ramp-up, well under a millisecond.

**Split `Radio.Send` into a prepare and a fire step at driver level (packet written ahead, only `SetTx` at the fire instant).**
Deferred: the driver's radio state (`SubgRf`) is private to the third-party `radio.c`.
Standby with the oscillator running already removes the variable part; the rest of `Radio.Send` is a fixed command sequence, and its duration is the measured Tx ramp.

## Consequences

- **Tx lead is at most `MAX_GUARD_TIME_MS`** (compile-time check), so a Tx wake stays within gaps sized for the guard (every gap >= 2 x `MAX_GUARD_TIME_MS`).
- **Wake look-ahead has a Tx case.** Rx slots wake a guard early, all others one Tx lead early: Skip slots are not told apart from Tx slots by the look-ahead, and their wake time does not matter.
- **Late fire instant.** If the slot task reaches the fire instant after it passed, `TX_LATE` is logged; a Sync packet is dropped and its compliance charge refunded, any other packet is sent late.
- **`WaitUntilMs` is on every Tx path** (it was a safety net for a Tx slot woken a guard early), with the RTC's 1/4096 s resolution.
- **C3's epoch is the nominal phase start.** The RTC still gives the date at phase entry, now read a Tx lead before the nominal start: a Sync phase starting less than `TX_LEAD_MS` after midnight carries the previous day's date (issue #28 covers date roll-over).
- **C3's first slot starts one Tx lead after the boot wake**, so its first packet is not late.
- **Tx ramp is a platform constant** (`TX_RAMP_MS` in `subghz_phy_task.c`, 0 until measured as `TX_DONE` start − `SYNC_TX` send).
- **Tx lead value.** 20 ms against a measured worst case of 13 ms from wake to on-air; `TX_LATE` shows when it is too short, and issue #46's runs can lower it.
