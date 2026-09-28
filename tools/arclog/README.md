# arclog

Host tool for ArcLog, the structured trace format of the ArcLoRaM firmware.
It records the serial output of each node, classifies every line, merges several nodes on one timeline and produces the sync run report for issue #21.

The line format and the event vocabulary are defined in `Common/Log/arclog.h` and documented in `CONTEXT.md` (ArcLog).

## Setup

The tool needs Python 3.10+ and [uv](https://docs.astral.sh/uv/).
It works the same on Linux, WSL and Windows.

```sh
cd tools/arclog
uv sync            # creates .venv with pyserial (and pytest for development)
uv run arclog --help
```

Without uv: `python -m pip install -e .` then run `arclog` (or `python -m arclog`).

### Serial ports

- Windows: ports are `COM3`, `COM5`, ... (Device Manager, "Ports (COM & LPT)").
- Linux: `/dev/ttyUSB0`, `/dev/ttyACM0`, ... (`ls /dev/tty{USB,ACM}*`); your user must be in the `dialout` group.
- WSL2: USB devices are not visible until attached from Windows with [usbipd-win](https://github.com/dorssel/usbipd-win):
  `usbipd list`, then `usbipd bind --busid <id>` (admin, once) and `usbipd attach --wsl --busid <id>`.
  For months-long captures, prefer running the tool natively on Windows: no attach step to repeat after replugs or reboots.

The trace UART is 9600 8N1 (`--baud` to change).

## Commands

### capture

Record one node.
Every line is stored verbatim, prefixed with its host UTC receive time.
Files rotate at UTC midnight (`<node>-YYYYMMDD.log`), and the port is reopened automatically after a USB replug or a board reset.

```sh
uv run arclog capture --port COM5 --node c3 --out runs/2026-09-25
uv run arclog capture --port COM6 --node c2 --out runs/2026-09-25 --level M
```

Run one capture per node, in two terminals.
The echo to the terminal accepts the same filters as `view`; the file always keeps everything.

### view

Show capture files, or a port live, classified and filtered.

```sh
uv run arclog view runs/2026-09-25/c2-20260925.log --mod Y,M
uv run arclog view --port COM6 --level M
uv run arclog view c2.log --event SYNC_RX,CLK,RTC_SET
```

| Filter | Meaning |
|---|---|
| `--core 0,4` | CM0+ / CM4 |
| `--mod T,M,Y,R,X,S,P` | TDMA, MAC, Sync, Radio, MbMux, System, Power |
| `--level L` | most verbose level shown: `A` < `L` < `M` < `H` |
| `--event SYNC_RX,CLK` | event names |
| `--grep text` | substring |
| `--no-legacy`, `--no-raw` | hide unconverted ST lines / unparsed lines |

Line kinds:

- **ArcLog** lines are shown with their fields.
- **LEGACY** lines have a device timestamp but free text: ST-generated traces outside CubeMX USER CODE regions, which cannot be converted.
- **RAW** lines are anything else.

Nothing is hidden unless you filter it.

Health notes are appended in the output:

- `!! N line(s) lost` when the per-core sequence number jumps (trace FIFO overflow on either core); it is reported even when the lost lines' neighbour is filtered out;
- `!! <schema problem>` when a line does not match the event table (firmware and tool out of step).

### merge

Interleave several nodes by host time.
Each received Sync packet (`SYNC_RX`) is matched to the transmission it came from by its protocol key (epoch, phase, cell), independent of the host clock.

```sh
uv run arclog merge runs/2026-09-25/*.log --mod Y,R
```

### report

Sync run report: acquisitions, drops to CLOCK_COLD, tier decisions, error statistics, drift in ppm, preamble-detection diagnostics, cross-node offsets and trace health.
Writes Markdown plus a CSV with one row per received Sync packet.

```sh
uv run arclog report runs/2026-09-25/*.log -o docs/experiments/2026-09-25-c3-c2.md --title "C3-C2 bench, run 1"
```

Daily files of the same node are concatenated automatically.

The trace health table gives each node's Node ID from its CM0+ `BOOT` line.
A board missing from the Node ID table boots with `id=0`; `view`, `merge` and `report` flag that `BOOT` line with the entry to add to `Common/Protocol/node_id.c`.

## Development

```sh
uv run pytest
```

`tests/test_schema.py` scans the firmware sources: every `ARCLOG()` event, module and key must be in `src/arclog/schema.py`, and no format may use a length modifier (`%lu`), which the firmware's formatter does not support.
When you add or change an event in the firmware, update `schema.py` in the same commit.
