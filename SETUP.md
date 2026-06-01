# Setup guide

Cross-platform install instructions for every flockdar tool, on **macOS**,
**Linux**, and **Windows**:

1. [The Python detector / TUI](#1-python-tool-tuipy) — analyse WiGLE data
2. [The ESP32 firmware toolchain](#2-esp32-firmware-toolchain) — build & flash the scanner
3. [USB serial drivers](#3-usb-serial-drivers) — talk to the ESP32
4. [Finding your serial port](#4-finding-your-serial-port)
5. [Live capture & SD-card replay](#5-live-capture--sd-card-replay)

---

## 1. Python tool (`flockdar`)

The detector, TUI, enrichment, and the serial ingest all ship in the
`flockdar` package (Python ≥ 3.11), managed by [uv](https://docs.astral.sh/uv/).

### Install uv

| OS | Command |
|---|---|
| **macOS** | `brew install uv` &nbsp;·&nbsp; or `curl -LsSf https://astral.sh/uv/install.sh \| sh` |
| **Linux** | `curl -LsSf https://astral.sh/uv/install.sh \| sh` |
| **Windows** | `powershell -c "irm https://astral.sh/uv/install.ps1 \| iex"` &nbsp;·&nbsp; or `winget install astral-sh.uv` |

Then, from the repo root:

```bash
uv sync                              # creates .venv and installs deps (incl. pyserial)
uv run flockdar wigle_backup.sqlite    # analyse a WiGLE Android SQLite backup
uv run flockdar WigleWifi_export.csv.gz
uv run pytest                        # run the test suite
```

`uv sync` is the only install step — it provisions Python itself if needed.

Or install the published package globally (no clone needed):

```bash
pipx install flockdar          # or: uv tool install flockdar
flockdar wigle_backup.sqlite
flockdar-ingest /dev/ttyUSB0   # headless serial/NDJSON ingest
```

---

## 2. ESP32 firmware toolchain

The firmware in `esp32/` builds with [PlatformIO](https://platformio.org/),
which downloads the Espressif toolchain, the Arduino core, and the NimBLE
library automatically on first build.

### Install PlatformIO Core (CLI)

PlatformIO is a Python package; install it with `uv` (or `pipx`):

```bash
uv tool install platformio --with pip   # pip required for tool-esptoolpy; gives you `pio` everywhere
# or: pipx install platformio
# or: VS Code -> Extensions -> "PlatformIO IDE"
```

If `pio run` fails with `No module named pip`, reinstall with `--with pip` or use
`pipx install platformio`. If `tool-esptoolpy` is corrupted, delete
`%USERPROFILE%\.platformio\packages\tool-esptoolpy` (Windows) or
`~/.platformio/packages/tool-esptoolpy` (macOS/Linux) and build again.

Per-OS notes:

- **macOS** — `brew install platformio` also works.
- **Linux** — install the udev rules so flashing works without `sudo`:
  ```bash
  curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules \
    | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
  sudo udevadm control --reload-rules && sudo udevadm trigger
  sudo usermod -aG dialout "$USER"     # then log out / back in
  ```
- **Windows** — install from PowerShell with `uv`/`pipx` as above, or use the
  VS Code PlatformIO IDE extension (bundles everything).

### Build by board

Each target is a PlatformIO **environment** (`-e <name>`). Full step-by-step
guides (build, flash, verify, Python pairing, troubleshooting) live in
**[esp32/BOARDS.md](esp32/BOARDS.md)**.

| Environment | Board | Guide |
|-------------|-------|--------|
| **`t-deck`** | LilyGO T-Deck / T-Deck Plus (display, GPS, SD, UI) | [T-Deck](esp32/BOARDS.md#lilygo-t-deck--t-deck-plus-envt-deck) |
| `esp32-s3` | Generic ESP32-S3 devkit (serial only) | [ESP32-S3](esp32/BOARDS.md#generic-esp32-s3-devkit-envesp32-s3) |
| `esp32-s3-sd` | ESP32-S3 + wired microSD | [ESP32-S3 + SD](esp32/BOARDS.md#esp32-s3--microsd-envesp32-s3-sd) |
| `esp32-s3-full` | ESP32-S3 + OLED + GPS + SD | [ESP32-S3 full](esp32/BOARDS.md#esp32-s3--oled--gps--sd-envesp32-s3-full) |
| `esp32` | Original ESP32 devkit | [ESP32](esp32/BOARDS.md#original-esp32-envesp32) |

All builds start with (from repo root):

```bash
uv run esp32/gen_oui_header.py
```

**T-Deck quick path** (wardrive handheld):

```bash
cd esp32
pio run -e t-deck -t upload --upload-port COM4   # adjust port; see BOARDS.md on Windows
pio device monitor -b 115200
```

Set `-DFD_HMAC_KEY=...` in `esp32/platformio.ini` before field use.

---

## 3. USB serial drivers

Most ESP32 dev boards expose USB via a **CP210x** (Silicon Labs) or **CH340/
CH9102** (WCH) USB-UART bridge. ESP32-S3 boards with native USB usually need
no driver at all.

| OS | CP210x | CH340 / CH9102 |
|---|---|---|
| **macOS** | Built in on modern macOS; else [SiLabs VCP driver](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers) | [WCH macOS driver](https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html) |
| **Linux** | `cp210x` & `ch341` modules ship with the kernel — just add yourself to `dialout` (see above) | same |
| **Windows** | [SiLabs CP210x VCP driver](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers) | [WCH CH340 driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html) |

After plugging in the board, confirm it enumerated (next section). If nothing
shows up, you usually need one of the drivers above.

---

## 4. Finding your serial port

| OS | How | Typical name |
|---|---|---|
| **macOS** | `ls /dev/cu.*` | `/dev/cu.usbserial-0001`, `/dev/cu.SLAB_USBtoUART` |
| **Linux** | `ls /dev/ttyUSB* /dev/ttyACM*` or `dmesg \| tail` after plugging in | `/dev/ttyUSB0`, `/dev/ttyACM0` |
| **Windows** | Device Manager → *Ports (COM & LPT)*, or `pio device list` | `COM3`, `COM5` |

`pio device list` (from PlatformIO) lists ports on every platform.

---

## 5. Live capture & SD-card replay

With the firmware flashed and the port identified:

```bash
# Live TUI as you drive (rings/notifies on each new HIGH-confidence device)
uv run flockdar --serial /dev/ttyUSB0          # macOS / Linux
uv run flockdar --serial COM3                  # Windows

# Headless: stream to a WiGLE-format SQLite the TUI can open later (Ctrl-C stops)
uv run python -m flockdar.serial_import /dev/ttyUSB0 capture.sqlite
uv run flockdar capture.sqlite

# Untethered: replay an SD-card log written by the esp32-s3-sd firmware
uv run flockdar flock-0001.ndjson              # open the NDJSON log directly
uv run python -m flockdar.serial_import flock-0001.ndjson capture.sqlite
```

The HMAC key must match the firmware's `-DFD_HMAC_KEY`. Override the receiver
key with `--key KEY` or `export FLOCKDAR_HMAC_KEY=...`; both sides default to
`flockdar-dev-key` for bench testing. Pass `--no-verify` to ingest unsigned or
mismatched-key streams during debugging.

If `--serial` reports a permission error on Linux, you are not in the
`dialout` group yet (see [section 2](#2-esp32-firmware-toolchain)).
