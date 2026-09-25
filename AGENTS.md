# Agent instructions

## Firmware builds

Never compile the firmware yourself.
This covers invoking `arm-none-eabi-gcc`, STM32CubeIDE `make`, headless CubeIDE builds, or any other firmware toolchain, even compile-only checks.
The firmware (CM0PLUS / CM4, all `Debug_C*` configurations) is built and flashed by Simon in STM32CubeIDE.
When a change needs a firmware build to verify it, say so and list what to check, then let Simon build it.

Host-side unit tests (`Tests/`, CMake + host GCC) are not firmware builds and may be run.
