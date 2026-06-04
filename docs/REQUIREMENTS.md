# System Requirements

## Goals

Passive RF detection of Flock Safety ALPR cameras across a distributed fleet of heterogeneous nodes. No active probing. No network dependency for core scanning function. All data stays under operator control.

## Non-goals

- Active attacks on detected devices
- Real-time vehicle tracking
- Community cloud data sharing (out of scope for v1; deliberate privacy decision)
- Windows support

---

## Functional Requirements

### FR-01: Detection

- **FR-01.1** Detect Flock Safety ALPR cameras via WiFi OUI matching, SSID pattern matching, BLE name/UUID matching, and manufacturer ID
- **FR-01.2** Assign a confidence level (HIGH / MEDIUM / LOW) to each detection based on signal combination
- **FR-01.3** Detect cameras that are asleep (hidden SSID + OUI + capability fingerprint)
- **FR-01.4** Detect Penguin BLE devices (mfgrid 2504, name prefix `Penguin-`)
- **FR-01.5** Signature data must be updateable without reflashing ESP32 firmware (SD card signature file, pulled from daemon on sync)

### FR-02: Node types

- **FR-02.1** Stationary hub node: WiFi Coconut (14-channel simultaneous 2.4GHz) + Alfa (5GHz) + BLE + Meshtastic GPS
- **FR-02.2** Mobile Pi node: single WiFi monitor mode interface + BLE, two radios (scan + management)
- **FR-02.3** T-Deck node: ESP32 promiscuous WiFi + BLE, SD loot log, opportunistic WiFi sync, LoRa mesh (future)
- **FR-02.4** Android node (future): BLE + active WiFi scan, offline loot, sync on reconnect

### FR-03: Storage and sync

- **FR-03.1** Every node stores detections locally before any sync attempt
- **FR-03.2** Sync to hub is opportunistic; scan-critical functions never block on connectivity
- **FR-03.3** T-Deck syncs when a trusted SSID is seen in scan results (Pwnagotchi pattern)
- **FR-03.4** Sync is incremental — only records since `last_synced_seq` are transmitted
- **FR-03.5** Sync gaps are logged as `gap` records with GPS coordinates and duration
- **FR-03.6** Daemon deduplicates by `(mac, run_id)`; cross-node observations of the same MAC are merged, not dropped

### FR-04: Run tracking

- **FR-04.1** Each scan session is a named Run with a ULID, start time, and GPS trace
- **FR-04.2** Run names are auto-generated (human-readable, memorable)
- **FR-04.3** Run history view: date, distance, new hits vs. previously seen MACs
- **FR-04.4** GPS trace exported with hit markers as GeoJSON / KML

### FR-05: Daemon

- **FR-05.1** Module system: each scanner type is an independently enabled module
- **FR-05.2** REST API: `/api/v1/hits`, `/api/v1/runs`, `/api/v1/nodes`, `/api/v1/sync`, `/api/v1/signatures/latest`
- **FR-05.3** WebSocket: live hit stream to connected frontends
- **FR-05.4** Embedded web UI: served from daemon binary, no separate install
- **FR-05.5** Runs as a systemd service; single binary, no runtime dependencies beyond `libpcap`

### FR-06: Web UI

- **FR-06.1** Leaflet map showing hit locations with confidence-coded markers
- **FR-06.2** Live hit feed (WebSocket)
- **FR-06.3** Run history with GPS track overlay (including gap segments)
- **FR-06.4** Node status dashboard: online/offline, last seen, hit count, GPS position
- **FR-06.5** Works in any modern browser; no build step, no framework, vanilla JS

### FR-07: T-Deck display (ESP32)

- **FR-07.1** Pwnagotchi-style mood display: happy on new hit, bored idle, excited near cluster, sleepy low battery
- **FR-07.2** Run stats overlay: hits this session, new vs. seen-before, GPS fix quality, battery
- **FR-07.3** "I remember you" reaction when a previously-seen MAC is detected
- **FR-07.4** Auto-generated run name shown on boot
- **FR-07.5** Button shortcuts: `S` stats, `R` new run, `C` connect/sync now

### FR-08: Security

- **FR-08.1** HMAC-SHA256 signature on every detection record (4-byte truncated)
- **FR-08.2** Per-node keys — compromising one node does not compromise others
- **FR-08.3** TLS for all WAN sync; LAN sync may use plain WebSocket
- **FR-08.4** Node config (keys, trusted SSIDs) stored with appropriate file permissions (chmod 600)
- **FR-08.5** No detection data leaves the operator's infrastructure without explicit configuration

---

## Non-functional Requirements

### NFR-01: Performance

- **NFR-01.1** Daemon handles ≥100 detection records/second ingest without dropping records
- **NFR-01.2** WiFi Coconut: all 14 interfaces processed concurrently (one goroutine per interface)
- **NFR-01.3** Web UI map renders smoothly with ≥10,000 hit markers

### NFR-02: Deployment

- **NFR-02.1** Daemon ships as a single statically-linked binary (or dynamically linked against `libpcap` only)
- **NFR-02.2** Cross-compiles to `linux/arm64` and `linux/amd64` from any platform
- **NFR-02.3** ESP32 firmware: OTA signature updates via SD card (no reflash for signature changes)
- **NFR-02.4** Daemon installable as a systemd service with a single command

### NFR-03: Reliability

- **NFR-03.1** No detection data lost due to sync failure or network interruption
- **NFR-03.2** Daemon survives individual module crashes (one goroutine panicking does not kill the process)
- **NFR-03.3** SQLite WAL mode; no corruption on unclean shutdown

### NFR-04: Observability

- **NFR-04.1** Structured JSON logging (zerolog or slog)
- **NFR-04.2** `/api/v1/stats` endpoint: uptime, records ingested, records by node, module status
- **NFR-04.3** Node dashboard shows last-heartbeat time per node

### NFR-05: Privacy

- **NFR-05.1** No telemetry, no opt-in data sharing, no required network access
- **NFR-05.2** WiGLE API key stored locally; never transmitted to daemon unless explicitly configured
- **NFR-05.3** GPS traces contain operator location history — stored locally only, never synced to external services by default
