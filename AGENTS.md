# Agent instructions

## Firmware builds

Build the firmware only in the bench Build Tree, `C:\Users\Simon\arcfw-bench\` (see `tools/bench`): a synced copy of the repo with its own CubeIDE workspace, built headless with `stm32cubeidec.exe`.
Once `bench build` exists, build only through it.
Never compile in the repo (its `Debug_C*` folders) or in Simon's CubeIDE workspace (`C:\Users\Simon\STM32CubeIDE\workspace_*`), and never invoke `arm-none-eabi-gcc` or `make` directly.

Flashing stays with Simon until `bench flash` exists (issue #63).
When a change needs a flash to verify it, say so and list what to check, then let Simon flash it.

Host-side unit tests (`Tests/`, CMake + host GCC) are not firmware builds and may be run.
