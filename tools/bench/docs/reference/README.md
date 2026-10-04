# Reference material

Third-party documents that `bench` depends on, kept verbatim so they can be read offline and compared with what the boards and Pi Nodes actually run.
Each entry says where it came from, when, and what in it matters here.

## openocd-README.txt

Source: https://openocd.org/doc-release/README, retrieved 2026-10-04.
OpenOCD is free software (GPL-2.0-or-later) and this file is part of its distribution.

Pi Nodes run OpenOCD (ADR-0002), which Pi-node builds from source.
What the README tells us:

- OpenOCD offers three network interfaces: telnet, Tcl and GDB.
  Pi-node disables telnet and Tcl because they take commands without a password, so `bench` uses the GDB port.
- The Raspberry Pi GPIO adapter (BCM2835) and the STM32 flash driver are listed as supported: the Pi path is upstream OpenOCD, not a fork.
- Running OpenOCD as root is strongly discouraged.
  Pi-node's service runs as the normal user.

What it does not tell us is the command set.
The `monitor` commands that the guard in `src/bench/pinode.py` allows (`reset halt`, `reset run`, a memory read) and refuses (`stm32l4x mass_erase`, `option_write`, `lock`, `unlock`) are documented in the User's Guide: https://openocd.org/doc/html/index.html.
Read the guide for the version that runs on the Pi (`openocd --version` there; the README does not name one) before extending the allowlist.
