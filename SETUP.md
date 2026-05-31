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

### Build & flash

```bash
uv run esp32/gen_oui_header.py        # sync oui_list.h with signatures.py
cd esp32
pio run -e esp32-s3                    # build (downloads toolchain first time)
pio run -e esp32-s3 -t upload          # flash a connected ESP32-S3
pio device monitor -b 115200           # watch the JSON stream

# peripheral envs:
pio run -e esp32-s3-sd   -t upload     # + microSD logging
pio run -e esp32-s3-full -t upload     # + OLED + GPS + microSD
```

Set the HMAC key shared with the Python receiver before field use — edit the
`-DFD_HMAC_KEY=...` flag in `esp32/platformio.ini`.

### LilyGO T-Deck / T-Deck Plus (`env:t-deck`)

```bash
uv run esp32/gen_oui_header.py
cd esp32 && pio run -e t-deck -t upload
```

#### Will this permanently damage the device?

**No — not in normal use.** Flashing flockdar is the same class of operation as
flashing Meshtastic or the LilyGO factory examples: PlatformIO writes a new
**application** image over USB. It does **not**:

- erase the whole chip (`erase_flash`) unless you run that yourself
- change eFuses or enable flash encryption
- flash the separate **keyboard MCU** (ESP32-C3) — only the main ESP32-S3 is updated
- reformat your microSD (it only appends `/flock-NNNN.ndjson` log files)

The realistic worst case is a **soft brick** (blank screen, no USB serial) until
you flash again — usually fixable with download mode + `pio run -t upload` or a
[Meshtastic](https://meshtastic.org/docs/getting-started/flashing-firmware/) /
LilyGO `.bin` from [T-Deck releases](https://github.com/Xinyuan-LilyGO/T-Deck).

**Before you flash (recommended):**

1. Flash over **USB with the power switch ON** (battery + USB is fine; avoid
   unplugging mid-write).
2. Use only **`pio run -e t-deck -t upload`** — do **not** run `erase`,
   `erase_flash`, or `pio run -t erase` unless you intend a full wipe.
3. Keep a recovery image handy (Meshtastic `firmware-t-deck-*-update.bin` or LilyGO
   factory build) if you want to go back.
4. If upload is unreliable on Windows, slow the port in `esp32/platformio.ini`:
   `upload_speed = 460800` under `[env:t-deck]`.

flockdar does not drive the LoRa radio for TX; it only scans WiFi/BLE and uses
the display, GPS, SD, and keyboard — all within the same pin map LilyGO documents.

#### Entering upload (download) mode

The T-Deck often **auto-resets** into the bootloader when `pio upload` starts.
If upload fails with “Failed to connect” or no serial port:

1. Connect **USB-C** to the PC.
2. Flip the **power switch ON**.
3. **Hold the center of the trackball** (boot strap — same as “BOOT” on other boards).
4. While holding, press the **RESET** button (left side; not the power switch).
5. Release **RESET**, then release the **trackball**.
6. Run `pio run -e t-deck -t upload` within a few seconds.

Alternative ([LilyGO T-Deck README](https://github.com/Xinyuan-LilyGO/T-Deck)):
hold trackball center, plug in USB, then upload.

After a successful flash, press **RESET** once if the screen stays black.

On Windows, confirm the port with `pio device list`. ESP32-S3 native USB usually
needs no extra driver on Win10+.

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
