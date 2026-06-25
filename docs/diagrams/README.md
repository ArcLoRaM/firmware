# ArcLoRaM State Machine Diagrams

Visual teaching aids for the ArcLoRaM TDMA-scheduled LoRa mesh protocol firmware (STM32WL55 dual-core).

## Study Order

| # | Diagram | Teaching Point |
|---|---------|---------------|
| 1 | [System Overview](01-system-overview.md) | Big picture: what lives on which core, what calls what |
| 2 | [MAC + Clock State Machine](02-mac-clock-state-machine.md) | Clock quality drives MAC transitions; three-tier sync dispatch |
| 3 | [TDMA Slot Execution](03-tdma-slot-execution.md) | The firmware heartbeat: what happens every RTC wake |
| 4 | [Boot to Operational](04-boot-to-operational.md) | How a node goes from power-on to fully paired |
| 5 | [Class Comparison](05-class-comparison.md) | Same interface, different behavior: C1 vs C2 vs C3 |
| 6 | [Inter-Core Communication](06-intercore-communication.md) | Single-writer ownership: race-freedom without mutexes |

## Rendering

These diagrams use **Mermaid** syntax. They render natively in:
- GitHub (just push and view the `.md` file)
- VS Code (install the "Markdown Preview Mermaid Support" extension)
- Notion, GitLab, Obsidian

To export as SVG or PNG for slides, paste the Mermaid block into [mermaid.live](https://mermaid.live).

## Source Code Cross-References

| Module | Source File |
|--------|------------|
| Protocol types (all enums) | `Common/Protocol/protocol_types.h` |
| MAC State Machine interface | `CM0PLUS/SubGHz_Phy/Logic/mac_state_machine.h` |
| MAC C1 (end node) | `CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c1.c` |
| MAC C2 (relay) | `CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c2.c` |
| MAC C3 (gateway) | `CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c3.c` |
| TDMA Machine | `CM0PLUS/SubGHz_Phy/Logic/tdma_machine.c` |
| Frequency Resolver | `CM0PLUS/SubGHz_Phy/Logic/freq_resolver.c` |
| Compliance Engine | `CM0PLUS/SubGHz_Phy/Logic/compliance_engine.c` |
| Shared Memory | `Common/SharedMemory/shared_mem.h` |
| Boot + callbacks | `CM0PLUS/SubGHz_Phy/App/subghz_phy_task.c` |
