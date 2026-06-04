# ADR-0008: LoRa as T-Deck communication channel

**Status:** Accepted  
**Date:** 2026-06-04

## Context

ADR-0004 defined an opportunistic WiFi sync strategy for the T-Deck: pause scanning, connect to a trusted WiFi AP, upload loot, disconnect, resume. This creates a scanning gap every time the device syncs — during the gap, the WiFi radio is in station mode and cannot capture 802.11 frames.

The T-Deck has a second radio: an SX1276 LoRa transceiver. Using LoRa for all Pi communication eliminates the scanning gap entirely — the WiFi radio never leaves monitor mode.

## Decision

**LoRa is the T-Deck's exclusive communication channel with the Pi hub.** WiFi sync (ADR-0004's trusted-SSID-pause-connect pattern) is **not implemented on the T-Deck**. The WiFi radio stays in promiscuous monitor mode for the full duration of any scan session.

Communication is bidirectional over LoRa:
- **T-Deck → Pi:** condensed hit records, GPS position, run status, heartbeat
- **Pi → T-Deck:** display updates (aggregated stats, mood input, run name, new-hit notifications)

The H2T Meshtastic node in the Pelican case (ADR-0007) serves as the LoRa gateway on the Pi side.

## Protocol: Meshtastic PRIVATE_APP channel

The H2T runs **stock Meshtastic firmware** and requires no modification. It relays arbitrary application data on named channels as a standard Meshtastic feature.

The T-Deck firmware (flockdar C++) encodes hit payloads as Meshtastic `MeshPacket` protobuf messages using portnum `PRIVATE_APP` (256) on the `flockdar` channel. The H2T receives these over LoRa and delivers them to the daemon via USB serial — treating the payload as opaque bytes, exactly as it does for any Meshtastic application. The `meshtastic-go` library on the Pi unwraps the Meshtastic envelope and hands the raw bytes to the daemon for decoding.

**The only custom code is in the T-Deck firmware** (encoding) **and the Pi daemon** (decoding). The H2T is a transparent relay running unmodified stock firmware.

**Channel configuration:**
- Channel name: `flockdar`
- Channel key: pre-shared 32-byte key (configured on both T-Deck and H2T)
- Band: 915MHz (US) — must match between T-Deck SX1276 and H2T SX1262

### Outbound packets (T-Deck → Pi)

**Hit record** (condensed, ≤200 bytes):
```
struct FlockdarHit {
  uint8_t  version = 1;
  uint8_t  type;          // 0=wifi, 1=ble
  uint8_t  mac[6];
  int8_t   rssi;
  uint8_t  channel;
  int32_t  lat_e7;        // latitude  * 1e7 (fixed point)
  int32_t  lon_e7;        // longitude * 1e7
  uint8_t  run_id[8];     // first 8 bytes of ULID (enough for dedup)
  uint8_t  hmac[4];       // truncated HMAC as in existing protocol
}
// total: 28 bytes — fits comfortably in a LoRa packet
```

**GPS update** (sent every 30s or on significant position change):
```
struct FlockdarGPS {
  uint8_t  version = 1;
  uint8_t  type = 0xFF;   // GPS update marker
  int32_t  lat_e7;
  int32_t  lon_e7;
  uint16_t accuracy_cm;
  uint8_t  fix_quality;   // 0=none, 1=2D, 2=3D
}
// total: 14 bytes
```

**Heartbeat** (sent every 60s):
```
struct FlockdarHeartbeat {
  uint8_t  version = 1;
  uint8_t  type = 0xFE;
  uint16_t hits_this_run;
  uint16_t hits_total;
  uint8_t  battery_pct;
  uint8_t  gps_fix;
}
// total: 8 bytes
```

### Inbound packets (Pi → T-Deck)

**Display update** (sent by Pi after receiving a hit or on 10s tick):
```
struct FlockdarDisplayUpdate {
  uint8_t  version = 1;
  uint8_t  type = 0x80;
  uint16_t total_hits;       // across all nodes this run
  uint16_t new_hits;         // new MACs not seen in prior runs
  uint8_t  mood;             // 0=bored, 1=normal, 2=happy, 3=excited
  uint8_t  nodes_online;
  char     run_name[16];     // null-terminated, truncated
}
// total: 24 bytes
```

The T-Deck display renders the `mood` field as a face and `total_hits` / `new_hits` as the stats overlay. The Pi has the full aggregated picture (Coconut + Alfa + BLE + T-Deck hits); the T-Deck display shows the fleet view, not just its own detections.

## LoRa link budget

At 915MHz, SF9, BW125, CR 4/5:
- Airtime for 28-byte hit packet: ~185ms
- Effective range: 1–5km urban, 5–15km open field
- Duty cycle limit (US 915MHz): no legal limit on 915MHz ISM; FCC Part 15 applies
- Practical throughput: ~5 packets/second maximum; flockdar generates far fewer hits than this in normal use

If the T-Deck detects a burst of hits (e.g. driving past a cluster of cameras), packets are queued in a small ring buffer. If the buffer fills, oldest unacknowledged hits are dropped — the SD card always has the complete record.

## SD card remains ground truth

LoRa transmission is **best-effort**. Every hit is written to SD before any LoRa transmit attempt. If a LoRa packet is lost (interference, out of range), the hit is not lost — it remains on SD. The daemon marks LoRa-received records as `via:lora` and reconciles against SD data when the card is read directly.

SD data retrieval options (future):
1. Physical card swap — always works, no software needed
2. Bulk LoRa transfer at end of session — slow (~1 hit/second) but possible for modest run sizes
3. If future hardware adds a second radio (USB WiFi OTG on T-Deck) — WiFi bulk sync without scanning gap

## Impact on ADR-0004

ADR-0004's "trusted SSID" WiFi sync pattern **does not apply to the T-Deck**. It may still apply to Platform B (Pi Zero 2W mobile node) where two radios allow WiFi sync without a scanning gap. The T-Deck's sync strategy is:

1. Real-time (best-effort): hits transmitted over LoRa as detected
2. Bulk: SD card physical retrieval or future LoRa bulk transfer

## Firmware and configuration changes required

**T-Deck firmware (C++) — custom code:**
- Implement Meshtastic `MeshPacket` protobuf encoding (`PRIVATE_APP` portnum, `flockdar` channel key)
- Binary hit/GPS/heartbeat structs in `protocol.h`
- Inbound packet handler — decode `FlockdarDisplayUpdate`, update display
- LoRa ring buffer (16 entries) for burst handling

**H2T — configuration only, no firmware changes:**
- Open Meshtastic app → connect to H2T → add `flockdar` channel with pre-shared key
- One-time setup, never touched again
- Stock Meshtastic firmware remains unchanged throughout the life of the project

## Consequences

- T-Deck WiFi radio is in monitor mode 100% of session time — no scanning gaps
- T-Deck display shows Pi fleet view, not just local hits — richer field experience
- LoRa range determines effective "tether" distance to Pi hub (~5km urban)
- SD card is always the authoritative complete record
- ADR-0004 WiFi sync applies to Pi Zero 2W (Platform B) only, not T-Deck
- H2T `flockdar` channel key must be provisioned on both H2T and T-Deck before deployment
