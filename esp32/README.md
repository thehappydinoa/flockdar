# flockdar-esp32

ESP32 firmware for passive Flock Safety camera detection. Companion hardware
to the `flockdar` Python tool — streams detections as JSON over USB serial
for real-time ingestion by the TUI.

> **Status: firmware implemented.** PlatformIO project under `src/` — WiFi
> promiscuous + BLE scanners, HMAC-signed JSON-over-serial, optional OLED and
> GPS. See the [roadmap](../ROADMAP.md) for what's next (OTA OUI refresh).

## Quick start

```bash
uv run esp32/gen_oui_header.py     # sync oui_list.h with signatures.py
cd esp32
pio run -e esp32-s3 -t upload      # build + flash an ESP32-S3 dev board
pio device monitor -b 115200       # watch the JSON stream

# with OLED + GPS wired up (see pin defines in src/config.h):
pio run -e esp32-s3-full -t upload
```

Set the HMAC key shared with the Python receiver before field use — edit the
`-DFD_HMAC_KEY=...` flag in `platformio.ini`. To lock to one channel instead
of hopping, uncomment `-DFD_FIXED_CHANNEL=6`.

---

## What it does

Runs two detection loops in parallel on an ESP32:

1. **WiFi promiscuous mode** — captures raw 802.11 management frames and
   checks both `addr2` (transmitter) and `addr1` (receiver) against the
   Flock OUI list. The addr1 check catches cameras even while they sleep —
   they appear as the *destination* of probe responses from nearby APs.
   Also catches wildcard probe requests (SSID tag length = 0) from cameras
   waking to upload.

2. **BLE scanner** — scans advertisements and matches Flock device names
   (`FS Ext Battery`, `Penguin-*`) and manufacturer ID 2504.

Every detection is signed with an HMAC so the Python receiver can reject
corrupted or spoofed frames. An optional GPS module tags each detection with
coordinates.

---

## Output format

Newline-delimited JSON at 115200 baud. Each line is one event.

```json
{"v":1,"type":"wifi","method":"probe_request","mac":"70:c9:4e:00:00:01","rssi":-72,"channel":6,"oui":"70:c9:4e","ts_ms":1234567890,"sig":"a3f2..."}
{"v":1,"type":"wifi","method":"addr1","mac":"d8:f3:bc:00:00:01","rssi":-68,"channel":11,"ts_ms":1234567891,"sig":"b81c..."}
{"v":1,"type":"ble","method":"name_match","mac":"04:0d:84:00:00:01","name":"FS Ext Battery","rssi":-85,"ts_ms":1234567892,"sig":"c940..."}
{"v":1,"type":"ble","method":"mfgrid","mac":"d4:b2:73:00:00:01","name":"1069698414","mfgrid":2504,"rssi":-90,"ts_ms":1234567893,"sig":"d12e..."}
{"v":1,"type":"gps","lat":40.0016,"lon":-74.0008,"alt":12.3,"accuracy":3.1,"ts_ms":1234567894}
```

Fields:

| Field | Description |
|---|---|
| `v` | Protocol version (currently 1) |
| `type` | `wifi`, `ble`, or `gps` |
| `method` | `probe_request`, `addr1`, `addr2`, `name_match`, `mfgrid` |
| `mac` | Lowercase colon-separated MAC address |
| `rssi` | Signal strength in dBm |
| `channel` | 802.11 channel (wifi) |
| `oui` | First 3 octets of MAC |
| `name` | BLE advertisement name |
| `mfgrid` | BLE manufacturer ID (decimal) |
| `ts_ms` | Device milliseconds since boot |
| `sig` | HMAC-SHA256 truncated to 8 hex chars (excludes `sig` field itself) |

`wifi` and `ble` lines are signed; `gps` and `info` lines are not. The
signature covers the complete JSON object *before* the `sig` field is inserted.
To verify, strip the trailing `,"sig":"<8 hex>"` (regex-replace
`,"sig":"[0-9a-f]{8}"}` → `}`) and recompute `HMAC-SHA256(key, line)[:4]` as
hex with the shared key.

---

## Hardware

### Minimum

| Part | Notes |
|---|---|
| ESP32-S3 dev board | S3 preferred — more RAM, better BLE throughput |
| USB-C cable | Power + serial to host |

### Recommended

| Part | Purpose |
|---|---|
| SSD1306 0.96" OLED (I²C) | Live detection count, last MAC, channel |
| u-blox NEO-6M or MTK3339 GPS | Location tagging per detection |
| microSD module (SPI) | Untethered NDJSON logging |
| 18650 LiPo + TP4056 | Portable operation without USB host |
| External 2.4 GHz antenna | +3–6 dB range improvement |

**Pin assignments, wiring, electrical, and RF detail:** see
[`HARDWARE.md`](HARDWARE.md). All peripherals are 3.3 V. GPIO maps are validated
against each chip's reserved pins by `pin_spec.py` and generated into
`src/pins.h` — never hand-edit pins.

---

## Detection logic

### WiFi (promiscuous mode)

```
For each 802.11 management frame:
  1. Skip if addr1 is multicast  (byte[0] bit 0 set)
  2. Skip if addr1 is locally administered (byte[0] bit 1 set)
  3. Check addr2 (transmitter) against OUI list  → emit addr2 detection
  4. Check addr1 (receiver)    against OUI list  → emit addr1 detection
  5. If Probe Request AND SSID tag length == 0   → emit probe_request
```

Channel strategy: hop 1 → 6 → 11 (2.4 GHz primary) with 800 ms dwell.
Lock to a single channel with `FD_FIXED_CHANNEL` at compile time.

### BLE

Standard NimBLE passive scan. On each advertisement:

1. Match device name against `BLE_NAME_PATTERNS` from `oui_list.h`
2. Match manufacturer ID against `FLOCK_MFGRIDS` from `oui_list.h`
3. Emit signed detection JSON

### OUI list

`oui_list.h` is auto-generated from `signatures.py` by running:

```bash
uv run esp32/gen_oui_header.py
```

Re-run this whenever `signatures.py` is updated so the firmware and Python
tool stay in sync. The device can also fetch a fresh `signatures.json` from
a configurable URL at boot time (OTA OUI update, v0.4 feature).

---

## Integration with flockdar Python

```bash
# Live TUI — reads from the device as you drive
uv run flockdar --serial /dev/ttyUSB0        # Linux / macOS
uv run flockdar --serial COM3                # Windows

# Headless logger -> WiGLE-format SQLite the TUI can open (Ctrl-C to stop)
uv run flockdar-ingest /dev/ttyUSB0 output.sqlite
uv run flockdar output.sqlite
```

The `flockdar.serial_import` module converts each JSON line to a `Hit` via the existing
`detect.analyze()` pipeline, verifying the HMAC signature first (`gps` lines
update the running position that detections inherit). All TUI features —
clustering, enrichment, OSM contribution, GeoJSON export — work identically on
live and archived data. Live mode rings the bell and notifies on each new
HIGH-confidence device.

The HMAC key must match the firmware's `-DFD_HMAC_KEY` build flag. Set it for
the receiver with `--key`, the `$FLOCKDAR_HMAC_KEY` env var, or leave the
default (`flockdar-dev-key`) on both sides for bench testing.

### Offline (SD card) wardriving

Build the `esp32-s3-sd` (or `esp32-s3-full`) env to log untethered — the
firmware writes the same newline-delimited JSON to `/flock-NNNN.ndjson` on a
microSD card (a fresh file per boot). Replay it later:

```bash
uv run flockdar-ingest flock-0001.ndjson output.sqlite
uv run flockdar flock-0001.ndjson       # or open the NDJSON log directly
```

The TUI opens `.ndjson` / `.jsonl` / `.log` files natively, so an SD log needs
no conversion step.

---

## Build system

PlatformIO with the `espressif32` platform. Two `uv`-based generators keep the
firmware in sync with the Python sources and validate the hardware design:

```bash
uv run esp32/gen_oui_header.py     # signatures.py -> oui_list.h (OUIs/names/mfgrids)
uv run esp32/pin_spec.py validate  # check GPIO maps against each chip's rules
uv run esp32/pin_spec.py gen       # write the validated src/pins.h
```

```
esp32/
  platformio.ini        PlatformIO config (envs: esp32-s3, esp32, esp32-s3-sd, esp32-s3-full)
  HARDWARE.md           Pin spec, electrical, RF design document
  gen_oui_header.py     Generates oui_list.h from signatures.py
  oui_list.h            Auto-generated — do not edit
  pin_spec.py           Authoritative pin map + validator (source of truth for pins.h)
  src/
    main.cpp            Entry point — setup scanners, drain the detection queue
    config.h            Compile-time behaviour knobs (HMAC key, channel, bauds)
    pins.h              Auto-generated, board-conditional GPIO map — do not edit
    protocol.h          Detection record + shared FreeRTOS queue
    match.cpp/.h        OUI / mfgrid / BLE-name matching against oui_list.h
    wifi_scanner.cpp/.h Promiscuous mode + frame parsing + channel hop
    ble_scanner.cpp/.h  NimBLE passive advertisement scanner
    gps.cpp/.h          GPS NMEA parser (FD_ENABLE_GPS)
    sdlog.cpp/.h        microSD NDJSON logger (FD_ENABLE_SD)
    signing.cpp/.h      HMAC-SHA256 frame signing
    display.cpp/.h      On-device UI (OLED or T-Deck TFT)
    tdeck_ui.cpp/.h     T-Deck display + trackball + keyboard (FD_ENABLE_TDECK_UI)
    tdeck_board.h       LilyGO GPIO constants (factory utilities.h)
    serial_out.cpp/.h   JSON serialisation + signing + output (serial + SD)
```

`tests/test_pins.py` re-runs validation and checks `src/pins.h` is in sync, so
a bad pin or a stale header fails CI.

---

## Research references

- NitekryDPaul — addr1 receiver technique and WiFi OUI list
  ([flock-you dataset](https://github.com/DeflockJoplin/flock-you/blob/main/datasets/NitekryDPaul_wifi_ouis.md))
- NSM-Barii — BLE name patterns, WiFi probe-request approach
  ([flock-back](https://github.com/NSM-Barii/flock-back))
