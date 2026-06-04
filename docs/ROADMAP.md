# Roadmap

Phases are sequential dependencies. Each phase ships something useful on its own.

---

## Phase 1 — Daemon foundation
*Unlocks: Pi as hub, existing ESP32 serial works, web UI accessible from phone*

- [ ] New Go repository, module layout (`cmd/`, `internal/`, `web/`, `esp32/`)
- [ ] Wire protocol types (`internal/protocol/`) — NDJSON structs, HMAC verify, `node_id`, `run_id`
- [ ] SQLite schema (`internal/store/`) — `networks`, `runs`, `gaps`, `nodes`, `mac_history`
- [ ] Daemon core (`internal/daemon/`) — module registry, detection channel, goroutine supervisor
- [ ] Serial module (`internal/modules/serial/`) — ingest from USB-attached ESP32, same as current `serial_import.py`
- [ ] REST API (`internal/api/`) — `/hits`, `/runs`, `/nodes`, `/stats`, `/sync`
- [ ] WebSocket live feed — new hits pushed to connected browsers
- [ ] Embedded web UI (`web/`) — hit list, node status panel, live feed. Leaflet map placeholder.
- [ ] `flockdard` binary — systemd unit file, install script
- [ ] CI: `go test ./...`, `go vet`, `staticcheck`

**Done when:** `flockdard --module serial:/dev/ttyUSB0` ingests from an ESP32 and the web UI on port 8080 shows hits in real time.

---

## Phase 2 — Linux scanner modules
*Unlocks: Pi scans without ESP32, WiFi Coconut fully utilized*

- [ ] WiFi monitor mode module (`internal/modules/wifi/`) — `gopacket` + libpcap, single interface
- [ ] WiFi Coconut module (`internal/modules/coconut/`) — auto-detect by USB VID/PID, one goroutine per interface, fan-in to detection channel
- [ ] BLE module (`internal/modules/ble/`) — BlueZ via D-Bus or `tinygo-bluetooth`, match against signature data
- [ ] Detection engine port (`internal/detect/`) — Go port of `signatures.py` + `detect.py` logic; `gen_oui_header.py` generates a Go source file alongside `oui_list.h`
- [ ] Alfa AWUS036ACH: verify works as standard monitor mode interface (no special handling needed if driver is loaded)
- [ ] Platform B (Pi Zero 2W) build target tested

**Done when:** `flockdard --module coconut --module ble` on a Pi 4 with Coconut attached detects Flock devices with no ESP32 in the loop.

---

## Phase 3 — Run tracking + map UI
*Unlocks: WiGLE-style wardrive history, GPS trace visualization*

- [ ] Run management — auto-start on scan begin, ULID, human-readable name generator
- [ ] GPS position manager — fallback chain: Meshtastic → USB NMEA → none
- [ ] Meshtastic module (`internal/modules/meshtastic/`) — connect to H2T via serial, position packets → GPS manager, mesh channel → detection ingest
- [ ] `gap` record storage and API
- [ ] Leaflet map in web UI — hit markers (confidence-coded), GPS track, gap overlays
- [ ] Run history view — date, distance, new vs. seen-before
- [ ] GeoJSON + KML export endpoints
- [ ] Cross-run MAC history (`mac_history` table, "seen 4× across 3 runs")

**Done when:** A complete wardrive session with GPS is visible on the map with the GPS track, hit markers, and gap segments. Run history shows multiple sessions.

---

## Phase 4 — ESP32 WebSocket sync + display personality
*Unlocks: T-Deck syncs wirelessly, Pwnagotchi-style experience*

- [ ] ESP32 firmware: trusted network list from SD card (`trusted_nets.json`)
- [ ] ESP32 firmware: opportunistic sync — pause scan, connect, POST gzip loot, pull signatures, disconnect, resume
- [ ] ESP32 firmware: `gap` record emission around sync windows
- [ ] ESP32 firmware: signature OTA — pull from `GET /api/v1/signatures/latest`, reload from SD without reflash
- [ ] Daemon: `POST /api/v1/sync` endpoint — gzip NDJSON ingest, idempotent, returns `last_seq`
- [ ] Daemon: `GET /api/v1/signatures/latest` — hash + current signature blob
- [ ] T-Deck display personality: mood states (happy, bored, excited, sleepy)
- [ ] T-Deck display: run stats overlay, "I remember you" reaction, run name on boot
- [ ] T-Deck button shortcuts: `S`, `R`, `C`
- [ ] Node dashboard in web UI: per-node status, last sync time, hit count

**Done when:** T-Deck in field auto-syncs when home WiFi is in range, display shows mood + run stats, daemon web UI shows node as online with last-sync timestamp.

---

## Phase 5 — Android node
*Unlocks: Phone as BLE scanner + field UI*

- [ ] Android app (Flutter or native Kotlin) — BLE scanner, active WiFi scan
- [ ] Offline loot storage (SQLite on device)
- [ ] Sync to daemon on WiFi reconnect
- [ ] Map view consuming daemon REST API
- [ ] Live hit feed via daemon WebSocket

**Note:** Android WiFi is active-scan only (no monitor mode on stock Android). BLE detection is full capability. Scope Android as a BLE-primary node.

**Done when:** Android app scans BLE, stores offline, syncs to Pi hub, visible in node dashboard.

---

## Phase 6 — LoRa mesh relay
*Unlocks: T-Deck hit alerts reach hub even without WiFi*

- [ ] T-Deck firmware: encode condensed hit as LoRa packet (MAC + RSSI + GPS + run_id, fits ≤250 bytes)
- [ ] T-Deck firmware: broadcast on `flockdar` Meshtastic channel
- [ ] Meshtastic module (daemon): receive mesh packets, ingest as `via:lora_mesh` records
- [ ] `via` field in web UI — distinguish mesh-relayed vs. direct detections
- [ ] Merge logic: when WiFi sync arrives for a mesh-relayed MAC, merge records

**Done when:** T-Deck 2km from hub with no WiFi — Pelican case web UI shows hit within seconds via LoRa, marked as mesh-relayed. Later WiFi sync fills in full detail.

---

## Phase 7 — Signature feed
*Unlocks: Community-contributed detection updates without code changes*

- [ ] Signature format: versioned JSON blob (`signatures.json`) containing OUIs, BLE names, SSID patterns, confidence tiers
- [ ] `gen_oui_header.py` extended to also generate `signatures.json` from `signatures.py`
- [ ] Daemon: serve current `signatures.json` at `/api/v1/signatures/latest`
- [ ] Daemon: `--signatures-url` flag to pull from a remote URL on startup (GitHub raw, self-hosted)
- [ ] ESP32: load `signatures.json` from SD card (replaces compiled-in `oui_list.h` for dynamic updates)
- [ ] Signature contribution guide

**Done when:** New Flock OUI discovered → added to `signatures.py` → `signatures.json` published → all nodes pull update without reflash or recompile.

---

## Unscheduled / future

- Remote C2 (self-hosted VPS) — multi-operator hub, nodes phone home over internet
- Per-node key provisioning UI
- e-ink display driver for Platform B (Pi Zero 2W)
- Raspberry Pi Zero 2W as dedicated field node image (single SD card flash, ready to go)
- PCAP capture mode — save raw frames for offline analysis
- WiGLE import compatibility — read existing WiGLE SQLite/CSV exports into the Go store
- `flockdar` CLI binary — TUI for local file analysis (port of Python Textual TUI)
