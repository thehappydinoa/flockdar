# ADR-0005: Reference hardware platforms

**Status:** Accepted  
**Date:** 2026-06-04

## Context

The ecosystem needs defined reference platforms with known-good hardware configurations. Ad-hoc hardware support leads to untested combinations and support burden. Three tiers are defined: stationary hub, mobile Pi, and ultraportable field device.

## Decision

Three reference platforms. The Go daemon runs on all Pi-class platforms. The ESP32 firmware runs on T-Deck class devices.

---

### Platform A — Stationary Hub ("The Pelican")

**Use case:** Home base, vehicle deployment, fixed installation. Maximum coverage. Not battery-constrained.

| Component | Part | Notes |
|---|---|---|
| Compute | Raspberry Pi 4B 4GB | Primary platform |
| 2.4GHz scanner | Hak5 WiFi Coconut | 14 simultaneous channels, USB 3.0 |
| 2.4/5GHz scanner | Alfa AWUS036ACH | Monitor mode, external antenna |
| BLE scanner | USB BT5 adapter | BlueZ, dedicated radio |
| GPS + mesh | Muzi Works H2T (Heltec T114 V2) | Meshtastic node, built-in GPS, LoRa 915MHz, 2000mAh internal battery |
| Storage | 128GB microSD | NDJSON logs + SQLite |
| Hub | Sabrent 60W powered USB hub | Powers Coconut + peripherals independently |
| Power | 12V LiPo 20Ah + dual DC-DC buck (5.1V/5A) | Pi and hub powered independently |
| Enclosure | Pelican 1510 carry-on | With fan grille (2x 40mm Noctua), panel-mount SMA, USB-C charge port, power switch |

**Power draw:** ~19–27W total. Runtime: ~8–10h on 20Ah 12V LiPo (240Wh).

**Connectivity:** Serves web UI on LAN. Accepts sync from field nodes. H2T provides GPS + acts as LoRa mesh gateway for T-Decks out of WiFi range.

---

### Platform B — Mobile Pi ("The Bag Node")

**Use case:** Wardriving on foot or in a vehicle without the full Pelican rig. Fits in a backpack.

| Component | Part | Notes |
|---|---|---|
| Compute | Raspberry Pi Zero 2W | Low power, ARM64 |
| WiFi scanner | Alfa AWUS036ACH (USB-A OTG) | Monitor mode; built-in Pi radio used for management/sync |
| GPS | GT-U7 GPS module (UART via USB adapter) | ~$10 |
| Display | Waveshare 2.13" e-ink HAT | Run stats, mood display |
| Battery | Pisugar 3 | LiPo, fits under Zero, ~5h |
| Storage | 64GB microSD | |

**Key advantage over T-Deck:** Two radios — one scans, one stays connected. No scanning gap during sync. Full Python/Go stack, no firmware constraints.

**Power draw:** ~4–6W. Runtime: ~5h on Pisugar 3.

---

### Platform C — Ultraportable Field Device ("The T-Deck")

**Use case:** Grab-and-go wardriving. Purpose-built enclosure, keyboard, display. Best when you want something pocketable that just works.

| Component | Part | Notes |
|---|---|---|
| Hardware | LILYGO T-Deck or T-Deck Plus | ESP32-S3 + keyboard + display + LoRa |
| GPS | Built-in (T-Deck Plus: L76K or u-blox M10) | Auto-detected in firmware |
| Storage | microSD | NDJSON loot log |
| Firmware | flockdar ESP32 (C++) | Pwnagotchi-style display personality |

**Key limitation:** Single WiFi radio — cannot scan and sync simultaneously. Sync gap is logged as a `gap` record. This is acceptable for field use; Platform B is preferred when gap-free coverage matters.

---

### Platform D — Android (future)

**Use case:** Phone-based scanning when carrying any other platform is impractical.

| Capability | Status | Notes |
|---|---|---|
| BLE scanning | Full | Standard Android API |
| WiFi active scan | Partial | Probe responses only, no monitor mode |
| WiFi monitor mode | Not supported | Requires root or custom driver |
| GPS | Full | Phone GPS |
| Sync to daemon | Full | WiFi reconnect to hub |

Android is a first-class node for BLE detection and GPS contribution. Its WiFi capability gap vs. ESP32/Pi is a known limitation, not a bug.

---

## Unsupported hardware (scoped out)

| Device | Why not |
|---|---|
| Hak5 Bash Bunny | No external USB ports for radios; payload delivery tool, not scanner |
| Hak5 DNS Driveby | DNS attack tool, wrong problem domain |
| Hak5 WiFi Pineapple | Could work but creates an active AP — contradicts passive detection goal |

## Consequences

- CI build matrix: `linux/arm64` (Pi 4, Pi Zero 2W), `linux/amd64` (laptop/server)
- Daemon tested on Pi 4B as primary target
- WiFi Coconut module: auto-detects 14 interfaces by USB VID/PID, spins up one goroutine per interface
- Meshtastic module: connects to H2T via `/dev/ttyUSB*`, reads position + mesh packets
- e-ink display driver: optional, activated by `--module eink` on Platform B
