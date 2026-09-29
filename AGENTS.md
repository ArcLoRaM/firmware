# Agent instructions

## Firmware builds and boards

Build, flash and reset the firmware only through `bench` (`tools/bench`, skill `bench`).
It builds in its own Build Tree on `C:` and talks to the boards through a guarded programmer, so the repo's `Debug_C*` folders and Simon's CubeIDE workspace stay his.
Bench refuses anything irreversible (option bytes, OTP, protection, mass erase); that refusal is final.

Host-side unit tests (`Tests/`, CMake + host GCC) are not firmware builds and may be run.
