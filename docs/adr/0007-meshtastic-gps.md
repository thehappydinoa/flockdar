# ADR-0007: Meshtastic node as GPS source and mesh relay

**Status:** Accepted  
**Date:** 2026-06-04

## Context

The stationary hub (Platform A) needs a GPS source and a way to receive hit alerts from T-Decks that are out of WiFi range. Two options: a dedicated USB GNSS dongle, or a Meshtastic node that provides GPS and LoRa mesh communication.

## Decision

Use a **dedicated Meshtastic node** (Muzi Works H2T / Heltec T114 V2) as:

1. Primary GPS source for the hub via the `meshtastic` Go library over USB serial
2. LoRa mesh gateway — receives hit packets from T-Decks in the field and delivers them to the daemon

The H2T connects to the Pi via USB-C. No separate GPS dongle required.

## Device: Muzi Works H2T

| Spec | Value |
|---|---|
| Board | Heltec Mesh Node T114 V2 |
| SoC | nRF52840 (ARM Cortex-M4) |
| Radio | SX1262 LoRa |
| Band | 915MHz (US) or 868MHz (EU) |
| GPS | Heltec GPS module (built-in) |
| Battery | 2000mAh LiPo internal |
| Connection | USB-C serial to Pi |
| BT | BLE 5.0 (nRF52840) |

**The internal 2000mAh battery is a meaningful operational benefit:** the H2T stays powered and maintains GPS fix during Pi reboots, power hiccups, or before the daemon starts. It can also run standalone as a pure Meshtastic node when the Pi is off.

## Meshtastic as GPS source

The `meshtastic-go` library connects to the H2T via serial, subscribes to position packets, and provides a continuous GPS stream to the daemon's position manager. The daemon uses this position to geotag hits detected by local scanner modules (WiFi Coconut, BLE, Alfa).

Fallback priority if H2T is unavailable:
1. H2T position packets (primary)
2. USB GNSS dongle via NMEA (`/dev/ttyUSB*`, secondary)
3. No GPS — hits stored without coordinates, flagged as `gps_unavailable`

## Meshtastic as mesh relay

T-Decks in the field that are out of WiFi range can broadcast condensed hit alerts over LoRa on a dedicated flockdar Meshtastic channel. The H2T receives these and the daemon ingests them as low-detail hit records:

```json
{
  "v": 1,
  "type": "wifi",
  "node_id": "t-deck-van",
  "run_id": "01J4X9K2...",
  "mac": "aa:bb:cc:dd:ee:ff",
  "rssi": -72,
  "lat": 40.001,
  "lon": -74.002,
  "via": "lora_mesh",
  "sig": "a1b2c3d4"
}
```

The `via: lora_mesh` field marks these as mesh-relayed. When the T-Deck later syncs over WiFi, the full SD log fills in any missing detail. The daemon merges by `(mac, run_id)`.

**LoRa payload size constraint:** LoRa packets are limited to ~250 bytes. The condensed mesh hit record (MAC + RSSI + GPS + run_id + node_id + HMAC truncated to 4 bytes) fits within this budget. Full detection detail (capabilities, BLE services, etc.) is deferred to the WiFi sync.

## Why not T-Beam instead

The LILYGO T-Beam is a common Meshtastic recommendation. The H2T (Heltec T114 V2) is preferred here because:

- nRF52840 has better BLE implementation than ESP32 for Meshtastic
- SX1262 has better sensitivity than SX1276 in older T-Beams
- Smaller form factor fits better inside the Pelican case
- The user already owns the H2T

## Consequences

- Daemon gains `meshtastic` module (goroutine connecting to H2T via serial)
- Daemon position manager has fallback chain: H2T → USB GNSS → none
- `via` field added to protocol schema (additive, non-breaking)
- T-Deck firmware: future enhancement to broadcast condensed hits over LoRa (Phase 4)
- Meshtastic channel name: `flockdar` (configured on all nodes via Meshtastic app)
- H2T antenna: SMA pigtail routed to panel-mount on Pelican case wall
