# flockdar-esp32

Planned ESP32 firmware module for passive Flock Safety camera detection.
Companion hardware to the `flockdar` Python tool — outputs JSON over USB
serial that the Python project can ingest in real time.

> **Status: design phase.** No firmware yet — this document captures the
> architecture decisions before code is written.

---

## What it does

Runs two parallel detection loops on an ESP32:

1. **WiFi promiscuous mode** — captures raw 802.11 management frames and
   checks both `addr2` (transmitter) and `addr1` (receiver) against the
   Flock OUI list. Catches cameras even while they sleep (the
   NitekryDPaul `addr1` technique). Also detects wildcard probe requests
   (SSID tag length = 0) from waking cameras.

2. **BLE scanner** — scans BLE advertisements and matches device names
   (`FS Ext Battery`, `Flock`, `Penguin`) and manufacturer IDs (2504).

Detections are emitted as newline-delimited JSON over USB serial at
115200 baud. Optional GPS module tags each detection with coordinates.

---

## Output format

One JSON object per line, compatible with `flockdar` Python import:

```json
{"type":"wifi","method":"probe_request","mac":"70:c9:4e:79:e7:66","rssi":-72,"channel":6,"oui":"70:c9:4e","ts_ms":1234567890}
{"type":"wifi","method":"addr1","mac":"d8:f3:bc:7d:c1:a9","rssi":-68,"channel":11,"ts_ms":1234567891}
{"type":"ble","method":"name_match","mac":"04:0d:84:2b:c2:a4","name":"FS Ext Battery","rssi":-85,"ts_ms":1234567892}
{"type":"ble","method":"mfgrid","mac":"d4:b2:73:d1:ef:3d","name":"1069698414","mfgrid":2504,"rssi":-90,"ts_ms":1234567893}
{"type":"gps","lat":39.9416,"lon":-75.1758,"alt":12.3,"accuracy":3.1,"ts_ms":1234567894}
```

`method` values:

- `probe_request` — wildcard probe request (SSID len=0) from Flock OUI
- `addr1` — Flock OUI observed as *receiver* in management frame (asleep camera)
- `addr2` — Flock OUI observed as *transmitter*
- `name_match` — BLE advertisement name matched
- `mfgrid` — BLE manufacturer ID matched

---

## Hardware

### Minimum (WiFi + BLE only)

| Part | Purpose | Notes |
|---|---|---|
| ESP32-WROOM-32 or ESP32-S3 | Main MCU | Dual-core; S3 preferred for RAM |
| USB-C breakout or dev board | Power + serial | Any ESP32 dev board works |

### Recommended additions

| Part | Purpose |
|---|---|
| SSD1306 0.96" OLED (I²C) | Live detection count / MAC display |
| u-blox NEO-6M or MTK3339 | GPS for location tagging |
| 18650 LiPo + TP4056 | Portable operation |
| External 2.4 GHz antenna | Improves detection range |

Compatible hardware variants from FlockSquawk (inspiration):

- M5StickC Plus2 (built-in display)
- M5Stack FIRE (built-in speaker + display)
- Any ESP32 dev board + I²C OLED

---

## Detection logic

### WiFi (promiscuous mode)

```
For each 802.11 management frame:
  1. Skip if addr1 is multicast (byte[0] bit 0 set)
  2. Skip if addr1 is locally administered (byte[0] bit 1 set)
  3. Check addr2 (transmitter) against OUI list → emit addr2 detection
  4. Check addr1 (receiver) against OUI list  → emit addr1 detection
  5. If frame is Probe Request AND SSID tag length == 0 → emit probe_request
```

Channel strategy: hop channels 1→6→11 (2.4 GHz primary) with 800 ms
dwell. Optionally lock to a single channel with `FD_FIXED_CHANNEL`.

### BLE

Standard NimBLE advertisement scan. On each advertisement:

1. Check device name against `BLE_NAME_PATTERNS`
2. Check manufacturer ID against `BLE_MFGRID_LIST`
3. Emit detection JSON

---

## Integration with flockdar Python

```bash
# Live ingest from serial port (planned feature)
uv run tui.py --serial /dev/ttyUSB0

# Or pipe to import tool
python -m flockdar.serial_import /dev/ttyUSB0 output.sqlite
```

The serial reader will convert incoming JSON lines into `Hit` objects
using the same `detect.analyze()` pipeline, so all existing TUI
features (clustering, enrichment, OSM contribution) work on live data.

---

## OUI list

The firmware embeds a compiled OUI lookup table derived from
`signatures.py:FLOCK_CHIP_OUIS`. A build script (`esp32/gen_oui_header.py`)
will regenerate `oui_list.h` from the canonical Python source so they
stay in sync.

---

## Differences from FlockSquawk

FlockSquawk is the primary inspiration. flockdar-esp32 will differ by:

- JSON output format designed to be consumed by `flockdar` Python tool
- `addr1` matching enabled by default (NitekryDPaul technique)
- GPS integration built into the output protocol
- Manufacturer ID (mfgrid) BLE matching in addition to name matching
- Build system uses PlatformIO (same as FlockSquawk) with a uv-based
  Python toolchain for OUI header generation

---

## References

- [FlockSquawk](https://github.com/f1yaw4y/FlockSquawk) — primary inspiration
- [flock-you](https://github.com/DeflockJoplin/flock-you) — OUI list and addr1 technique
- [flock-back](https://github.com/NSM-Barii/flock-back) — BLE + WiFi probe approach
- [NitekryDPaul WiFi OUIs](https://github.com/DeflockJoplin/flock-you/blob/main/datasets/NitekryDPaul_wifi_ouis.md)
