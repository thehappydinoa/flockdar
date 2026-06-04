# ADR-0003: Wire protocol — HMAC-signed NDJSON over WebSocket

**Status:** Accepted  
**Date:** 2026-06-04

## Context

The existing ESP32-to-host protocol is HMAC-SHA256 signed newline-delimited JSON over USB serial. As the ecosystem expands to include network-connected nodes (Pi scanner, Android, remote ESP32 over WiFi), the protocol needs to work over multiple transports without redesign.

Candidates evaluated:

- **Extend current NDJSON** — add fields, keep format
- **Protocol Buffers / gRPC** — binary, schema-enforced, strongly typed
- **MQTT** — pub/sub, broker required
- **MessagePack** — binary JSON equivalent, no schema

## Decision

**HMAC-signed NDJSON** remains the canonical wire format. Transport is **WebSocket** for network connections, **USB serial** for wired ESP32. No broker required.

Two new fields added to every detection record:

```json
{
  "v": 1,
  "type": "wifi",
  "node_id": "t-deck-van",
  "run_id": "01J4X9K2...",
  "mac": "aa:bb:cc:dd:ee:ff",
  "rssi": -72,
  "channel": 6,
  "ts": 1748995200,
  "sig": "a1b2c3d4"
}
```

| Field | Description |
|---|---|
| `node_id` | Stable name assigned in node config. Human readable (`pi-garage`, `t-deck-van`). |
| `run_id` | ULID generated at scan-start. Groups all detections in one wardrive session. |
| `ts` | Unix timestamp seconds. ESP32 gets this from GPS; Pi uses system clock. |

GPS position record (unchanged except `node_id` / `run_id`):
```json
{"v":1,"type":"gps","node_id":"t-deck-van","run_id":"01J4X9K2...","lat":40.0,"lon":-74.0,"accuracy":3.1,"ts":1748995200}
```

Sync/loot batch: a gzip-compressed stream of NDJSON records POSTed to `POST /api/v1/sync`. The daemon deduplicates by `(mac, run_id)` on ingest.

## Rationale

### Why not Protobuf/gRPC

Protobuf would require a schema compiler in the ESP32 build chain. The ESP32 currently has no protobuf dependency and adding one complicates the PlatformIO build significantly. NDJSON is debuggable with `cat`, inspectable in logs, and already implemented on both sides.

### Why not MQTT

MQTT requires a broker process. The whole point of the architecture is that nodes work standalone and sync opportunistically — a broker is a single point of failure that contradicts that goal. WebSocket direct-connect is simpler and sufficient.

### Why WebSocket over plain TCP

WebSocket is HTTP-upgradable, works through most firewalls and proxies, and has mature libraries in Go, Python, and JavaScript. The daemon's web UI uses the same WebSocket endpoint for live hit streaming to browsers, so there's no second protocol to implement.

## HMAC key management

The current single shared key (`flockdar-dev-key`) is replaced with **per-node keys**:

- Each node is provisioned with a unique 32-byte key (hex string in config)
- The daemon maintains a `nodes.json` mapping `node_id → key`
- Compromising one node's key does not compromise others
- Key rotation: update `nodes.json` on daemon, push new config to node

For local LAN sync (trusted network), HMAC verification is still enforced — the transport being local does not remove the integrity check.

## Schema versioning

The `v` field is the protocol version. Current: `1`. The daemon rejects records with unknown versions and logs a warning. Version negotiation is out of scope for v1 — bump `v` and handle both in the daemon when a breaking change is needed.

## Consequences

- ESP32 firmware gains `node_id` and `run_id` fields (compile-time `node_id`, runtime `run_id` from boot)
- Python `serial_import.py` updated to pass through `node_id` / `run_id` (non-breaking, fields are additive)
- Go daemon validates HMAC on every ingest path (serial, WebSocket, sync POST)
- All existing NDJSON logs from the ESP32 remain parseable (new fields default to empty/unknown)
