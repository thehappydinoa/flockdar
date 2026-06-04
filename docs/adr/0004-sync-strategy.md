# ADR-0004: Offline-first sync — opportunistic loot model

**Status:** Accepted (amended by ADR-0008)  
**Date:** 2026-06-04

## Context

Nodes operate in environments where network connectivity is intermittent — a T-Deck wardriving a neighborhood has no reliable WiFi link to a hub. Data must never be lost due to connectivity gaps.

## Decision

Every node stores detections locally first. Sync to a hub is **opportunistic** and **never a hard dependency**.

### Storage per node type

| Node | Local storage | Sync trigger |
|---|---|---|
| T-Deck (ESP32) | SD card NDJSON log | Sees trusted SSID in scan results |
| Pi Zero 2W | SQLite (`~/.local/share/flockdar/loot.db`) | Always-on if hub reachable; periodic batch otherwise |
| Pi 4 hub | SQLite (primary store) | Accepts inbound sync; optionally syncs to remote C2 |
| Android (future) | SQLite on device | WiFi reconnect to trusted network |

> **Amendment (ADR-0008):** The WiFi sync pattern below **does not apply to the T-Deck**. The T-Deck communicates exclusively via LoRa (using its SX1276 radio and the H2T as gateway), keeping its WiFi radio in monitor mode at all times. The pattern below applies to Platform B (Pi Zero 2W) which has two WiFi radios and can sync on one while scanning on the other.

### T-Deck sync flow (Pwnagotchi pattern)

```
scanning loop:
  passive WiFi + BLE capture → SD log

  if trusted_ssid seen in scan results:
    pause scan (gap logged in run metadata with GPS coords)
    connect to AP
    POST /api/v1/sync  (gzip NDJSON, records since last_synced_seq)
    server returns: {accepted: N, last_seq: M}
    store last_synced_seq = M on SD
    GET /api/v1/signatures/latest  (pull signature updates if hash differs)
    disconnect
    resume scan
    display: "synced {N} hits ★"

  if sync fails (timeout, auth error):
    log warning, continue scanning
    retry on next trusted SSID sighting
```

Trusted SSIDs stored in `/flockdar/trusted_nets.json` on SD card:
```json
[
  {"ssid": "HomeNetwork", "key": "node-key-hex"},
  {"ssid": "OfficeWifi",  "key": "node-key-hex"}
]
```

No reflash needed to add a trusted network — edit the SD card file.

### Deduplication

The daemon deduplicates on `(mac, run_id)`. A MAC seen by two different nodes in the same approximate timeframe produces two records with different `node_id` values — both are kept, merged into a single `Hit` with multiple `signals` entries noting both observations. This is more informative than silently dropping one.

Cross-run deduplication (same MAC seen in two different wardrive sessions) is tracked in the `mac_history` table: first_seen run, last_seen run, total observations. The TUI/web UI can show "seen 4 times across 3 runs."

### Sync gap logging

When the T-Deck pauses to sync, it emits a `gap` record to the SD log:
```json
{"v":1,"type":"gap","node_id":"t-deck-van","run_id":"...","start_ts":1748995200,"end_ts":1748995207,"lat":40.0,"lon":-74.0,"reason":"wifi_sync"}
```

The daemon stores these in a `gaps` table. The map UI renders gap intervals as dashed segments on the GPS track — you know exactly where the blind spots were.

## Rationale

This model is proven by Pwnagotchi (same offline-first, sync-on-home-network pattern) and Hak5 devices (loot queue, exfil on reconnect). The key properties:

- **No data loss**: SD card is ground truth; sync failure never drops a detection
- **No hub dependency**: nodes work fully standalone indefinitely
- **Auditable**: gap records make coverage holes explicit rather than silently missing
- **Incremental**: `last_synced_seq` means sync is always a small delta, not a full re-upload

## Consequences

- ESP32 firmware: add `gap` record emission around sync window; store `last_synced_seq` in SPIFFS or SD
- Daemon: `POST /api/v1/sync` endpoint, idempotent (re-uploading already-seen records is a no-op)
- Daemon: `gaps` table in SQLite schema
- Web UI: map renders GPS track with gap overlays
- Signature pull on sync: daemon exposes `GET /api/v1/signatures/latest` returning a hash + NDJSON blob of current OUI/pattern data; ESP32 reloads `oui_list` from SD if hash differs (no reflash needed for signature updates)
