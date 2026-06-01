# Building flockdar firmware by board

PlatformIO **environments** (`-e <name>`) select the target hardware. All envs share
the same detection core (WiFi promiscuous + BLE); peripherals and UI differ.

| Environment | Hardware | Display | GPS | SD log | Serial JSON |
|---------------|----------|---------|-----|--------|-------------|
| **`t-deck`** | [LilyGO T-Deck / T-Deck Plus](https://github.com/Xinyuan-LilyGO/T-Deck) | Onboard TFT + UI | Onboard | Yes | USB CDC |
| `esp32-s3` | Generic ESP32-S3 devkit | — | — | — | USB/UART |
| `esp32` | Generic ESP32 devkit | — | — | — | USB/UART |
| `esp32-s3-sd` | ESP32-S3 + wired microSD | — | — | Yes | USB/UART |
| `esp32-s3-full` | ESP32-S3 + wired OLED, GPS, SD | SSD1306 | UART | Yes | USB/UART |

Pin maps for generic boards: `src/config.h` and `pin_spec.py` → `src/pins.h`.
T-Deck pins: `src/tdeck_board.h` (not generated from `pin_spec.py`).

Before any build (all boards):

```bash
uv run esp32/gen_oui_header.py    # sync oui_list.h + vendor_list.h from signatures.py
```

Set `-DFD_HMAC_KEY=...` in `platformio.ini` to match the Python receiver before
field use. Optional: uncomment `-DFD_FIXED_CHANNEL=6` to stop channel hopping.

---

## LilyGO T-Deck / T-Deck Plus (`env:t-deck`)

**Best for wardriving** — everything is on the board: ST7789 display, trackball
keyboard, GPS, microSD, WiFi/BLE. Builds the full **flockdar** UI (Status / Hits /
Nearby carousel) plus HMAC-signed NDJSON on USB and optional SD logs.

### What this firmware enables

- Passive WiFi + BLE Flock detection (same signatures as the Python TUI)
- On-screen hit list, nearby RF devices, confidence / PROBE badges
- GPS-stamped `wifi` / `ble` lines when fix is available
- SD files: `/flock-0001.ndjson`, … (FAT32 card, 32 GB or less recommended)
- USB serial at **115200** baud for `flockdar --serial` or `flockdar-ingest`

### Prerequisites

1. **PlatformIO** — from repo root:
   ```bash
   uv tool install platformio --with pip
   ```
   See [SETUP.md §2](../SETUP.md#2-esp32-firmware-toolchain) for pip/esptoolpy
   fixes and udev on Linux.

2. **USB data cable** — short USB-C, direct to the PC (avoid unpowered hubs).

3. **T-Deck** — power switch **ON** when using or flashing.

4. **microSD (optional)** — FAT32, ≤32 GB, for untethered logging.

### Build (no device required)

```bash
cd esp32
pio run -e t-deck
```

Output: `.pio/build/t-deck/firmware.bin`

First run downloads `espressif32@6.3.0`, NimBLE, vendored TFT_eSPI, etc. The
`t-deck` env uses LilyGO’s `T-Deck` board definition and **patched TFT_eSPI** in
`vendor/` (upstream `bodmer/TFT_eSPI` leaves the panel blank on this display).

### Flash

```bash
cd esp32
pio device list                              # note COM* / cu.* / ttyACM*
pio run -e t-deck -t upload                  # auto port
pio run -e t-deck -t upload --upload-port COM4   # Windows example
```

**macOS / Linux** — automatic upload usually works.

**Windows** — if build succeeds but upload fails with COM “busy” or error 31, use
[Upload troubleshooting (Windows)](#upload-troubleshooting-windows) below.

After flash: press **RESET** once if the screen stays black; confirm the **power
switch is ON**. You should see the flockdar boot splash, then the Status screen.
Trackball **+** / **-** adjusts backlight.

### Serial monitor

Close the monitor before re-flashing.

```bash
cd esp32
pio device monitor -b 115200
```

### Use with the Python TUI

```bash
# Live wardrive UI on the laptop (device shows its own UI in parallel)
uv run flockdar --serial COM4              # Windows
uv run flockdar --serial /dev/ttyACM0      # Linux

# Headless capture to SQLite
uv run flockdar-ingest COM4 wardrive.sqlite
uv run flockdar wardrive.sqlite

# Replay SD log from the card
uv run flockdar flock-0001.ndjson
```

HMAC key must match `platformio.ini` (`flockdar-dev-key` by default) or pass
`--key` / `FLOCKDAR_HMAC_KEY`.

### Flash safety

`pio run -e t-deck -t upload` only replaces the **main ESP32-S3** application
(same class of risk as Meshtastic or LilyGO factory examples). It does **not**:

- erase the whole flash (`erase_flash`) unless you run that yourself
- flash the separate **keyboard MCU** (ESP32-C3)
- reformat the microSD (only appends `flock-*.ndjson`)

Keep a [Meshtastic](https://meshtastic.org/docs/getting-started/flashing-firmware/)
or LilyGO recovery `.bin` if you want an easy rollback.

### Entering upload (download) mode

**Always:** USB-C connected, **power switch ON**.

1. **Automatic** — `pio run -e t-deck -t upload` (best on macOS/Linux).

2. **Hardware BOOT (most reliable on Windows)** — BOOT = **center of the trackball**:
   - Hold trackball center → press **RESET** (left side) → release RESET → release
     trackball → upload within a few seconds.

3. **Power-off method** — power off, hold trackball center, plug USB while
   holding, release after 1–2 s, then upload.

4. **1200 baud touch** — when flockdar (or Meshtastic) is already running:
   ```bash
   python -c "import serial,time;p='COM4';s=serial.Serial(p,1200);s.dtr=False;s.rts=True;time.sleep(0.1);s.dtr=True;s.rts=False;s.close()"
   cd esp32 && pio run -e t-deck -t upload --upload-port COM4
   ```
   On Windows, prefer method 2 if COM stays in error 31.

### Upload troubleshooting (Windows)

Symptom: build OK, then:

```text
Could not open COM3, the port is busy or doesn't exist.
PermissionError(13, 'A device attached to the system is not functioning.', None, 31)
```

**Workflow:**

1. Unplug USB 5 s, replug; power switch **ON**.
2. Close Serial Monitor, `pio device monitor`, Meshtastic, Arduino IDE, PuTTY.
3. `pio run -e t-deck -t upload -v`
4. When log shows **Waiting for the new upload port…**, use hardware BOOT (above).
5. If needed: `pio device list` and `pio run -e t-deck -t upload --upload-port COMx`

Port health check:

```bash
uv run python esp32/scripts/upload_port_diag.py COM4
```

**Manual flash** (device already in download mode):

```bash
cd esp32
python -m esptool --chip esp32s3 -p COM4 -b 460800 write_flash 0x10000 .pio/build/t-deck/firmware.bin
```

`platformio.ini` sets `upload_speed = 460800` and `use_1200bps_touch` for this board.

### Clean rebuild

```bash
cd esp32
pio run -e t-deck -t clean
pio run -e t-deck
```

---

## Generic ESP32-S3 devkit (`env:esp32-s3`)

Minimal build — serial JSON only, no display/GPS/SD.

```bash
cd esp32
pio run -e esp32-s3 -t upload
pio device monitor -b 115200
```

## ESP32-S3 + microSD (`env:esp32-s3-sd`)

Adds `-DFD_ENABLE_SD`. Wire CS and SPI pins in `src/config.h`.

```bash
cd esp32
pio run -e esp32-s3-sd -t upload
```

## ESP32-S3 + OLED + GPS + SD (`env:esp32-s3-full`)

Adds OLED (SSD1306 I²C), GPS (UART), and SD. See `src/config.h` and
[HARDWARE.md](HARDWARE.md).

```bash
cd esp32
pio run -e esp32-s3-full -t upload
```

## Original ESP32 (`env:esp32`)

Same as `esp32-s3` but targets `esp32dev` (less RAM; prefer S3 for new builds).

```bash
cd esp32
pio run -e esp32 -t upload
```
