# ADR-0002: Hop-Count Windowing Is a Protocol State Machine Concern

## Status
Accepted

## Context
The Mesh_Uplink Phase divides transmission opportunities into three time windows
by hop-count mod 3, to mitigate the hidden node problem and prevent adjacent
layers from transmitting simultaneously. This rule could be encoded in the TDMA
Table (as Sub-Phases, one per hop group) or applied at runtime by the Protocol
State Machine.

## Decision
The TDMA Table stays flat: Frame → Phase → Cell → Slot. The Protocol State
Machine applies hop-count mod-3 eligibility at runtime. The table contains no
notion of hop count.

## Consequences
The windowing rule can change (different modulo, different grouping strategy)
without modifying the TDMA Table format or redefining any term in CONTEXT.md.
The table remains node-agnostic and readable without knowing network topology.
The rule is invisible to any tool that reads only the table — it is documented
here and implemented exclusively in the Protocol State Machine.
