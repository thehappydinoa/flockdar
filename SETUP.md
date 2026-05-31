# Setup guide

Cross-platform install instructions for every flockdar tool, on **macOS**,
**Linux**, and **Windows**:

1. [The Python detector / TUI](#1-python-tool-flockdar) — analyse WiGLE data
2. [The ESP32 firmware toolchain](#2-esp32-firmware-toolchain) — build & flash the scanner
3. [USB serial drivers](#3-usb-serial-drivers) — talk to the ESP32
4. [Finding your serial port](#4-finding-your-serial-port)
5. [Live capture & SD-card replay](#5-live-capture--sd-card-replay)
6. [Raspberry Pi + Meshtastic GPS](#6-raspberry-pi--meshtastic-gps)

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
uv tool install platformio            # gives you the `pio` command everywhere
# or: pipx install platformio
# or: VS Code -> Extensions -> "PlatformIO IDE"
```

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

---

## 6. Raspberry Pi + Meshtastic GPS

On a Raspberry Pi (or any always-on Linux host) flockdar can take live GPS from
a [Meshtastic](https://meshtastic.org/) node in the vehicle instead of wiring a
GPS to the ESP32 — handy when the node already has a GPS and is in the car.

Install with the optional `meshtastic` extra:

```bash
pipx install "flockdar[meshtastic]"      # or: uv tool install "flockdar[meshtastic]"
```

Plug in both the ESP32 scanner and the Meshtastic node over USB, then:

```bash
# Live TUI: detections from the ESP32, position from the Meshtastic node
flockdar --serial /dev/ttyUSB0 --meshtastic /dev/ttyACM0

# Headless logger to SQLite (flushes every 30 s; clean stop on SIGTERM)
flockdar-ingest /dev/ttyUSB0 ~/flock.sqlite --meshtastic /dev/ttyACM0

# Node reachable over the network instead of USB:
flockdar --serial /dev/ttyUSB0 --meshtastic-host 192.168.1.50
```

`--meshtastic` with no device auto-detects a USB node. flockdar tracks the
**local** node's fix (other GPS-equipped nodes in the mesh don't move your
position) and stamps it onto each detection, overriding the ESP32's own GPS if
present. If the node isn't reachable or the extra isn't installed, live mode
continues without GPS and warns once.

### Native scanning (no ESP32)

A Pi has the headroom to scan with its **own** radios, so you can skip the ESP32
entirely (or run it as well). Install the `pi` extra and the `iw` tool:

```bash
pipx install "flockdar[pi]"               # BLE via bleak
sudo apt install -y iw                    # Wi-Fi active scan
# allow Wi-Fi scanning without sudo (optional):
sudo setcap cap_net_admin,cap_net_raw+eip "$(which iw)"
```

Then:

```bash
# Live TUI from the Pi's Wi-Fi + Bluetooth, GPS from the Meshtastic node
flockdar --scan --meshtastic /dev/ttyACM0

# Headless logger to SQLite (BLE only on a Pi with no external Wi-Fi adapter)
flockdar-scan ~/flock.sqlite --no-wifi --meshtastic /dev/ttyACM0
```

- **Wi-Fi** uses periodic `iw dev wlan0 scan` (`--wifi-iface` to change, needs
  CAP_NET_ADMIN). It catches Flock camera SSIDs, `flocknet`, and chip OUIs.
- **BLE** uses a bleak passive scan — the strongest native signals (Raven GATT
  service UUIDs, Penguin manufacturer id 2504, `FS Ext Battery` names).
- Either scanner can fail to start (missing tool/extra, permissions) without
  stopping the other; the failure is shown as a notification.
- `--no-wifi` / `--no-ble` disable a scanner. The same `--meshtastic` GPS and
  SQLite flushing as the serial path apply.

> Note: `iw` active scanning sees *advertised* networks. The ESP32's promiscuous
> `addr1`-receiver and wildcard-probe techniques (catching sleeping cameras)
> need monitor mode and aren't done here — pair a Pi with the ESP32 for both.

To run either as a boot service, see [`deploy/README.md`](deploy/README.md) and
the [`deploy/flockdar-ingest.service`](deploy/flockdar-ingest.service) unit.
