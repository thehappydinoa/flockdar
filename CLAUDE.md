# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Project name: **flockdar** (passive RF detection of Flock Safety ALPR cameras).

## Commands

```bash
uv sync                                      # install dependencies
uv run tui.py <wigle.sqlite>                 # launch TUI
uv run tui.py <WigleWifi_export.csv.gz>      # TUI from CSV export
uv run python -c "import detect, tui, signatures; print('OK')"  # import check
uv run pytest                                # run tests
uv run esp32/gen_oui_header.py               # regenerate esp32/oui_list.h
```

There are no tests or linter config yet.

## Architecture

Three files, strict layering — UI never touches signatures directly:

```
signatures.py  →  detect.py  →  tui.py
```

**`signatures.py`** — pure data, no logic. All OUI prefixes, BLE service UUIDs, SSID patterns, and GATT characteristic tables live here. Edit this file when adding new Flock device signatures. Key exports: `FLOCK_DIRECT_OUIS`, `FLOCK_CHIP_OUIS`, `FLOCK_BACKHAUL_OUIS`, `RAVEN_SERVICES_HIGH`, `SURVEILLANCE_OUIS`.

**`detect.py`** — no UI imports. `analyze()` takes a single device record and returns a `Hit` (or `None`). `run_detection(path)` drives both input formats (SQLite via `read_sqlite`, CSV/CSV.gz via `read_csv`) and deduplicates by MAC. `export_csv` / `export_kml` are also here. The `Hit` dataclass computes `confidence` (1–3) from its `signals` list — signals are `(label, detail)` tuples appended by `analyze()`.

**`enrich.py`** — post-detection enrichment, no UI imports. Three `Enricher` subclasses mutate `Hit.signals` in-place: `OverpassEnricher` (OSM ALPR nodes via DeFlock's Overpass mirror, free), `ALPRWatchEnricher` (downloads daily KMZ from alprwatch.org, caches 24 h, parses KML placemarks), `WiGLEEnricher` (looks up MACs via WiGLE API, requires key). `build_enrichers()` assembles the list; `enrich_hits(hits, enrichers, callback)` runs them with per-hit progress callbacks. WiGLE credentials stored in `~/.config/flock-wigle/config.json` or env vars `WIGLE_API_NAME`/`WIGLE_API_TOKEN`.

**`tui.py`** — Textual app. `FlockDetectApp` loads data via `@work(thread=True)` so the UI stays responsive. `FilterPanel` checkboxes (including "Group nearby") drive `_rebuild_table()`, which converts filtered hits into `list[Cluster]` via either `detect.cluster_hits()` or `detect.single_clusters()`. `DataTable.RowHighlighted` updates `DetailPanel`. Key bindings: `m`/`v`/`c` (Maps/StreetView/copy MAC), `n` (enrich), `d`/`D` (discover/force-refresh), `o` (open OSM iD editor + copy tags), `w` (WiGLE config), `e`/`k`/`g` (CSV/KML/GeoJSON export).

**`discover.py`** — WiGLE discovery with disk cache. `WiGLEDiscovery.discover()` fetches BLE name + WiFi SSID results from WiGLE, converts to `Hit` objects via `analyze()`, and caches to `~/.cache/flock-wigle/wigle-discovery.json`. Cache is `partial=True` if rate-limited mid-run — served regardless of TTL with a retry hint. `save_cache()` is called after each individual query so work is never fully lost on 429.

**`esp32/`** — Design spec and tooling for the planned ESP32 companion firmware. `gen_oui_header.py` regenerates `oui_list.h` from `signatures.py` so OUI lists stay in sync between Python and C.

## Input formats

The tool reads two WiGLE data formats:

- **SQLite** (`.sqlite`/`.db`) — WiGLE Android app backup; richer, includes `service` column with BLE service UUIDs space-separated
- **CSV.gz** (`.csv.gz`/`.csv`) — WiGLE web export or app export; no service UUID data

The SQLite `network` table has a single-char `type` field: `W`=WiFi, `E`=BLE, `B`=Bluetooth Classic.

## Confidence scoring

`Hit.confidence` is derived from signal labels (not stored explicitly):

- **3 HIGH**: `FLOCK_DIRECT_OUI`, `RAVEN_UUID_HIGH`, `FLOCKNET_SSID`, `FLOCK_CAMERA_SSID`, `PENGUIN_BLE_SSID`
- **2 MEDIUM**: `FLOCK_CAMERA_SSID_PATTERN`, `BLE_NAME`, `BACKHAUL_OUI_HIDDEN`, `FLOCK_WIFI_FP`, `FLOCK_MFGRID`
- **1 LOW**: `CHIP_OUI`, `SSID_PATTERN`, `RAVEN_UUID_OLD`, `SURVEILLANCE_OUI`

Key signal notes:
- **`FLOCK_CAMERA_SSID`** — SSID matches `Flock-XXXXXX`/`FLOCK-XXXXXX` AND (suffix == last 6 MAC chars) OR (OUI in `FLOCK_CHIP_OUIS`). Both paths are HIGH.
- **`PENGUIN_BLE_SSID`** — BLE name matches `Penguin-\d{10}`. From flock-you Penguin dataset.
- **`FLOCK_MFGRID`** — BLE manufacturer ID 2504 (Penguin). Catches devices that drop the `Penguin-` prefix.
- **`FLOCK_WIFI_FP`** — OUI in `FLOCK_CHIP_OUIS` + capabilities `[WPA2-PSK-CCMP-128][RSN-PSK-CCMP-128][ESS]` + freq in {2412,2437,2462} MHz. Catches sleeping cameras with hidden SSIDs.
- **`BACKHAUL_OUI_HIDDEN`** — eero (`80:da:13`) with blank SSID only; named eero SSIDs skipped.

`Hit` stores `capabilities` and `mfgrid` (SQLite populates both; CSV gets `AuthMode` for capabilities, mfgrid=0).

## Detection methods not applicable to WiGLE data

**Wildcard probe request** — Flock cameras send 802.11 management frames with SSID tag length = 0 when waking to upload. Detectable only via ESP32 promiscuous mode (flock-you, FlockSquawk). WiGLE passive scans do not capture raw frames.

## Data files are gitignored

`*.sqlite`, `*.db`, `*.csv.gz` are in `.gitignore`. Never commit WiGLE exports — they contain personal location history.
