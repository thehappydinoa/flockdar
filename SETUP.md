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

#### LilyGO T-Deck / T-Deck Plus

```bash
cd esp32 && pio run -e t-deck -t upload
```

See [flash safety](#will-this-permanently-damage-the-device) and [upload mode](#entering-upload-download-mode) below.

#### Will this permanently damage the device?

**No — not in normal use.** `pio run -e t-deck -t upload` only replaces the main
ESP32-S3 application (like Meshtastic or LilyGO examples). It does not erase the
whole chip, change eFuses, flash the keyboard MCU (ESP32-C3), or format the SD
card. Worst case is a **soft brick** until you re-flash — keep a Meshtastic
`firmware-t-deck-*-update.bin` handy if you want an easy rollback. Do **not** run
`pio run -t erase` or `erase_flash` unless you mean to wipe everything.

#### Entering upload (download) mode

**Always:** device plugged in via **USB-C**, **power switch ON**.

The T-Deck is **ESP32-S3** with native USB. If `pio run -t upload` says it cannot
connect, try these in order:

**1. Automatic (try first)** — PlatformIO usually resets the chip for you:

```bash
pio device list          # note the COM / cu.* port
pio run -e t-deck -t upload --upload-port COM5   # Windows example
```

**2. Hardware BOOT (most reliable)** — On T-Deck, **BOOT = center of the trackball**
(GPIO0). Either:

- **Power-off method** (matches Espressif / PlatformIO hints): turn **off**, hold
  **trackball center**, plug in USB while still holding, release after 1–2 s, then upload.
- **RESET method** ([LilyGO](https://github.com/Xinyuan-LilyGO/T-Deck)): power **on**,
  hold **trackball center**, press **RESET** (left button), release RESET, release
  trackball, upload within a few seconds.

**3. 1200 baud reset** — Works when the device is already running *some* USB firmware
(Meshtastic, flockdar, factory test) and responds to the Arduino “1200 bps touch”:

```bash
# Replace COM5 with your port from `pio device list`
python -c "import serial, time; p='COM5'; s=serial.Serial(p,1200); s.dtr=False; s.rts=True; time.sleep(0.1); s.dtr=True; s.rts=False; s.close(); print('reset sent on', p)"
cd esp32 && pio run -e t-deck -t upload --upload-port COM5
```

On macOS/Linux use `/dev/cu.usbmodem*` or `/dev/ttyACM0` instead of `COM5`. Run
upload **immediately** after the one-liner (within ~3 s). If this does nothing, use
method 2 — hardware BOOT always works even when the app won’t start.

**After a successful flash:** press **RESET** once if the screen stays black.

#### Upload fails: `PermissionError(13)` / Windows error 31 on COM3

Your **build succeeded** — only the USB serial step failed. Error 31 usually
means Windows lost the port (reset to bootloader, USB glitch, or another program
has COM3 open).

1. **Close every app** that might hold the port: PlatformIO Serial Monitor,
   `pio device monitor`, Meshtastic web client (serial), Arduino IDE, PuTTY.
2. **Do not lock to COM3** if the number changes after reset — run
   `pio device list` and watch Device Manager while uploading; the bootloader
   port is often **COM4** or **COM5**, not the normal app port.
3. **Prefer auto-detect** (`[env:t-deck]` uses `wait_for_upload_port` and
   `use_1200bps_touch`):
   ```bash
   cd esp32
   pio run -e t-deck -t upload -v
   ```
   When the log says **“Waiting for the new upload port…”**, do **trackball BOOT +
   RESET**, then use whichever COM port appears.
4. **USB hardware:** short **data** USB-C cable, **USB 2.0** port on the PC (not
   an unpowered hub), or a **powered hub** if the link drops during connect.
5. **Unplug → wait 5 s → replug**, power ON, hardware BOOT, then upload.

Manual flash after download mode (replace `COM5`):

```bash
python -m esptool --chip esp32s3 -p COM5 -b 460800 write_flash 0x10000 .pio/build/t-deck/firmware.bin
```

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
