# flockdar-esp32 — Hardware Design Specification

Pin assignment, electrical, and RF specification for the flockdar-esp32
passive scanner. The GPIO assignments here are the human-readable view of the
machine-validated source of truth in [`pin_spec.py`](pin_spec.py); the firmware
header [`src/pins.h`](src/pins.h) is generated from it.

| | |
|---|---|
| **Status** | Released — validated, builds for two targets |
| **Primary target** | ESP32-S3 (ESP32-S3-WROOM-1, `esp32-s3-devkitc-1`) |
| **Secondary target** | ESP32 (ESP32-WROOM-32, `esp32dev`) |
| **Logic level** | 3.3 V on all I/O (no 5 V-tolerant pins) |
| **Validation** | `uv run esp32/pin_spec.py validate` (enforced in CI) |

---

## 1. System overview

```
                 ┌──────────────────────────────────────────┐
   2.4 GHz  ~~~► │ ESP32-S3                                   │
   (Wi-Fi/BLE)   │   • Wi-Fi promiscuous  (addr1/addr2/probe) │
                 │   • NimBLE passive scan                    │
                 │                                            │
   USB-C ◄──────►│   USB-Serial/JTAG  (JSON stream + power)   │
                 │                                            │
                 │   I2C ──► SSD1306 OLED   (status)          │
                 │   SPI ──► microSD        (NDJSON log)      │
                 │   UART ─► NMEA GPS       (lat/lon/accuracy)│
                 └──────────────────────────────────────────┘
```

All peripherals are optional and compile-guarded (`FD_ENABLE_OLED`,
`FD_ENABLE_SD`, `FD_ENABLE_GPS`). The radio is always on.

---

## 2. Pin assignment

Signal direction is from the **ESP32's** perspective. `DIR` drives the
input-only validation rule (an input-only pin may carry `in` but never `out`).

### 2.1 ESP32-S3 (primary — `esp32-s3-devkitc-1`)

| Function | Macro | GPIO | Dir | Bus | Notes |
|---|---|---|---|---|---|
| OLED SDA | `FD_OLED_SDA` | 8 | inout | I2C0 | |
| OLED SCL | `FD_OLED_SCL` | 9 | inout | I2C0 | |
| microSD CS | `FD_SD_CS` | 10 | out | SPI (FSPI) | |
| microSD SCK | `FD_SD_SCK` | 12 | out | SPI (FSPI) | |
| microSD MISO | `FD_SD_MISO` | 13 | in | SPI (FSPI) | |
| microSD MOSI | `FD_SD_MOSI` | 11 | out | SPI (FSPI) | |
| GPS RX | `FD_GPS_RX_PIN` | 18 | in | UART1 | from GPS TX |
| GPS TX | `FD_GPS_TX_PIN` | 17 | out | UART1 | to GPS RX |

All assignments are general-purpose pins clear of every reserved group below.

### 2.2 ESP32 (secondary — `esp32dev`)

| Function | Macro | GPIO | Dir | Bus | Notes |
|---|---|---|---|---|---|
| OLED SDA | `FD_OLED_SDA` | 21 | inout | I2C0 | |
| OLED SCL | `FD_OLED_SCL` | 22 | inout | I2C0 | |
| microSD CS | `FD_SD_CS` | 5 | out | VSPI | ⚠ strapping pin (boot pull-up; OK for CS) |
| microSD SCK | `FD_SD_SCK` | 18 | out | VSPI | |
| microSD MISO | `FD_SD_MISO` | 19 | in | VSPI | |
| microSD MOSI | `FD_SD_MOSI` | 23 | out | VSPI | |
| GPS RX | `FD_GPS_RX_PIN` | 16 | in | UART2 | unavailable on WROVER (PSRAM) |
| GPS TX | `FD_GPS_TX_PIN` | 17 | out | UART2 | unavailable on WROVER (PSRAM) |

---

## 3. GPIO constraint rationale

Pins were chosen to avoid every reserved/hazardous group. This is what the
validator enforces (ERROR = build-breaking, WARN = allowed with care).

### ESP32-S3

| Group | GPIOs | Why avoided | Severity |
|---|---|---|---|
| Non-existent | 22–25 | Not bonded out on the S3 | ERROR |
| SPI flash | 26–32 | Dedicated to the module's flash | ERROR |
| Octal PSRAM/flash | 33–37 | Used on `…R8` (octal) modules | WARN |
| Native USB | 19, 20 | USB-Serial/JTAG D−/D+ — the JSON link | WARN |
| Strapping | 0, 3, 45, 46 | Sampled at boot; wrong level blocks boot | WARN |
| UART0 console | 43, 44 | Default debug console | WARN |

The S3 has **no input-only pins** — every GPIO can drive an output.

### ESP32

| Group | GPIOs | Why avoided | Severity |
|---|---|---|---|
| SPI flash | 6–11 | Dedicated to the module's flash | ERROR |
| Input-only | 34, 35, 36, 39 | No output driver — invalid for `out` signals | ERROR |
| Strapping | 0, 2, 5, 12, 15 | Sampled at boot | WARN |
| UART0 console | 1, 3 | Default debug console | WARN |
| ADC2 | 0,2,4,12–15,25–27 | Unusable for analog while Wi-Fi is active | (n/a — no analog) |

We use no analog inputs, so the ADC2/Wi-Fi conflict does not apply, but it is
recorded so future analog additions are checked against it.

---

## 4. Peripheral interfaces

| Interface | Peripheral | Detail |
|---|---|---|
| **USB-CDC** | Host PC | 115200 baud, signed newline-delimited JSON (see [README](README.md)). On the S3 this is the native USB-Serial/JTAG; on the ESP32 it is the on-board CP210x/CH340 bridge on UART0. |
| **I2C0** | SSD1306 OLED | 100–400 kHz, address `0x3C` (`FD_OLED_ADDR`). 4.7 kΩ pull-ups to 3V3 on SDA/SCL (most OLED breakouts include them). |
| **SPI** | microSD | Default ~20 MHz (Arduino `SD`). Add a 10 kΩ pull-up on CS so the card stays deselected during boot. Use short leads; cards draw write bursts. |
| **UART1/2** | NMEA GPS | 9600 baud (`FD_GPS_BAUD`), 3.3 V TTL. RX↔TX crossed. u-blox NEO-6M / MTK3339. |
| **2.4 GHz radio** | on-chip | Wi-Fi promiscuous (channel hop 1/6/11) + BLE passive scan. PCB or external IPEX antenna. |

---

## 5. Electrical

- **Logic level:** 3.3 V everywhere. The OLED, microSD, and GPS modules used
  must be 3.3 V parts — do **not** wire 5 V signal lines to any GPIO.
- **Power:** USB-C 5 V → on-board 3V3 LDO. For untethered (SD-logging) use, an
  18650 Li-ion + TP4056 charger or a 5 V power bank on USB.
- **Current budget (peak, primary target):**

  | Block | Typical | Peak |
  |---|---|---|
  | ESP32-S3 Wi-Fi promiscuous + BLE | 100–160 mA | ~240 mA (TX bursts) |
  | SSD1306 OLED | 10–15 mA | 20 mA (all pixels) |
  | GPS (acquisition) | 25–40 mA | 50 mA |
  | microSD (write burst) | 20–50 mA | ~100 mA |
  | **System** | **~200 mA** | **~350 mA** |

  Size the supply for ≥ 500 mA. A 3000 mAh 18650 gives roughly 8–12 h of
  continuous scanning depending on peripherals.
- **Decoupling:** rely on the module's bypass caps; add 10 µF + 100 nF at the
  microSD socket — cards cause supply dips that can reset a marginal 3V3 rail.

---

## 6. Boot / strapping notes

- **ESP32-S3:** keep GPIO0/3/45/46 at their default levels at power-up. None
  are used by this design.
- **ESP32:** GPIO5 (microSD CS) is a strapping pin. It has a boot-time pull-up
  and idles high (card deselected), which is the safe state, so it is used for
  CS by long-standing convention. Do not also pull it low externally.

---

## 7. RF / antenna

- Operates only in the 2.4 GHz ISM band; the firmware hops channels 1→6→11
  (`FD_CHANNEL_DWELL_MS`) or locks to one with `-DFD_FIXED_CHANNEL`.
- Keep the module's PCB antenna (or IPEX connector + external whip) clear of
  ground pour, batteries, and the SD card. An external 2.4 GHz antenna adds
  ~3–6 dB of range.

---

## 8. Change process

`pin_spec.py` is the single source of truth. To change a pin or add a board:

1. Edit the board map (or add a `Board` / `ChipCaps`) in `esp32/pin_spec.py`.
2. `uv run esp32/pin_spec.py validate` — must report **PASS** (no ERRORs).
3. `uv run esp32/pin_spec.py gen` — regenerates `src/pins.h`.
4. Update §2 of this document.
5. `uv run pytest tests/test_pins.py` — the suite re-validates every board and
   checks that `src/pins.h` is in sync with the spec.

Never hand-edit `src/pins.h`.

---

## 9. Bring-up checklist

1. Flash `esp32-s3` (radio only): confirm the `info` banner and `wifi`/`ble`
   JSON lines on the serial monitor.
2. Add `-DFD_ENABLE_OLED`: confirm the status screen and I2C address.
3. Add `-DFD_ENABLE_SD`: confirm `"sd log open"` and a growing
   `/flock-NNNN.ndjson`.
4. Add `-DFD_ENABLE_GPS`: confirm `gps` lines once the module has a fix.
5. Verify HMAC: `uv run python -m flockdar.serial_import <port>` should show no
   rejected lines with the matching key.
