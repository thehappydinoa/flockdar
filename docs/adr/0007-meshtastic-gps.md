# ADR-0007: Meshtastic node as GPS source and mesh relay

**Status:** Accepted  
**Date:** 2026-06-04

## Context

The stationary hub (Platform A) needs a GPS source and a way to receive hit alerts from T-Decks that are out of WiFi range. Two options: a dedicated USB GNSS dongle, or a Meshtastic node that provides GPS and LoRa mesh communication.

## Decision

Use the **Muzi Works H2T (Heltec T114 V2) running stock Meshtastic firmware** as:

1. Primary GPS source for the hub via the `meshtastic-go` library over USB serial
2. LoRa mesh gateway — receives data packets from T-Decks in the field and delivers them to the daemon

The H2T connects to the Pi via USB-C. No separate GPS dongle required.

**The H2T runs unmodified stock Meshtastic firmware throughout. No custom firmware, no firmware modifications of any kind.** All configuration (channel name, channel key, node name) is done once via the Meshtastic phone app. The H2T can simultaneously participate in the broader Meshtastic mesh as a normal node.

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

T-Decks in the field send condensed hit records as standard Meshtastic data packets on the `flockdar` channel (`PRIVATE_APP` portnum 256). The H2T receives and relays these over the mesh without needing to understand their content — this is standard Meshtastic behaviour for application data.

The `meshtastic-go` library on the Pi receives the data packets via USB serial and the daemon decodes the binary payload into hit records marked `via:lora_mesh`.

**The H2T requires no knowledge of the flockdar protocol.** It treats flockdar packets as opaque application data, exactly as stock Meshtastic handles any third-party app using `PRIVATE_APP`.

## Why not T-Beam instead

The LILYGO T-Beam is a common Meshtastic recommendation. The H2T (Heltec T114 V2) is preferred here because:

- nRF52840 has better BLE implementation than ESP32 for Meshtastic
- SX1262 has better sensitivity than SX1276 in older T-Beams
- Smaller form factor fits better inside the Pelican case
- The user already owns the H2T

## H2T setup (one-time, via Meshtastic phone app)

1. Connect phone to H2T over BLE
2. Set node name: `flockdar-hub`
3. Add channel: name=`flockdar`, key=`<32-byte hex>`, role=PRIMARY or SECONDARY
4. Done — no further H2T interaction required

The same channel name and key must be configured on the T-Deck's LoRa radio (in flockdar firmware config, not Meshtastic app).

## Consequences

- Daemon gains `meshtastic` module (goroutine connecting to H2T via USB serial using `meshtastic-go`)
- Daemon position manager has fallback chain: H2T position packets → USB GNSS → none
- `via` field added to protocol schema (additive, non-breaking)
- **H2T firmware: never modified** — stock Meshtastic only
- T-Deck firmware: implements Meshtastic `PRIVATE_APP` packet encoding to send hits the H2T can relay (see ADR-0008)
- Meshtastic channel `flockdar` configured on H2T via phone app; key stored in T-Deck firmware config
- H2T antenna: SMA pigtail routed to panel-mount on Pelican case wall
- H2T continues to function as a normal Meshtastic node — other mesh users are unaffected
