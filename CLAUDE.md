# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Project name: **flockdar** (passive RF detection of Flock Safety ALPR cameras).
Two components: **host CLI** (`src/flockdar/`, PyPI) and **ESP32 firmware**
(`esp32/`). User-facing overview: `docs/README.md`, `README.md`.

## Commands

```bash
uv sync                                      # install package (editable) + deps
uv run flockdar <wigle.sqlite>               # launch TUI
uv run flockdar <WigleWifi_export.csv.gz>    # TUI from CSV export
uv run flockdar <flock-0001.ndjson>          # TUI from an ESP32 SD-card log
uv run flockdar --serial /dev/ttyUSB0        # live ESP32 capture
uv run flockdar-ingest <port|log> [out.sqlite]   # headless ingest (python -m flockdar.serial_import)
uv run python -c "import flockdar; print(flockdar.__version__)"  # import check
uv run pytest                                # run tests
uv run esp32/gen_oui_header.py               # regenerate esp32/oui_list.h
uv run esp32/pin_spec.py validate            # validate firmware GPIO maps
uv run esp32/pin_spec.py gen                 # regenerate esp32/src/pins.h
uv build                                     # build sdist + wheel into dist/ (PyPI)

cd esp32 && pio run -e esp32-s3 -t upload    # build + flash firmware (PlatformIO)
cd esp32 && pio run -e t-deck -t upload      # LilyGO T-Deck (see esp32/BOARDS.md)
```

Per-board build guides: `esp32/BOARDS.md`. Per-OS toolchain (uv, PlatformIO,
serial drivers): `SETUP.md`.

## Architecture

src-layout package `src/flockdar/` (installable, PyPI-ready via hatchling).
Within it, strict layering — UI never touches signatures directly:

```
signatures.py  →  detect.py  →  tui.py
```

> **README diagram** — `README.md` has a Mermaid module-dependency diagram in the **Files** section. Update it whenever a module is added, removed, or its dependency edges change.

Intra-package imports are relative (`from . import detect`); tests and external
code use absolute imports (`from flockdar import detect`). Console scripts:
`flockdar` → `flockdar.tui:main`, `flockdar-ingest` → `flockdar.serial_import:main`.
Version lives in `src/flockdar/__init__.py` (`__version__`, hatchling dynamic).

**`signatures.py`** — pure data, no logic. All OUI prefixes, BLE service UUIDs, SSID patterns, and GATT characteristic tables live here. Edit this file when adding new Flock device signatures. Key exports: `FLOCK_DIRECT_OUIS`, `FLOCK_CHIP_OUIS`, `FLOCK_BACKHAUL_OUIS`, `RAVEN_SERVICES_HIGH`, `SURVEILLANCE_OUIS`.

**`detect.py`** — no UI imports. `analyze()` takes a single device record and returns a `Hit` (or `None`). `run_detection(path)` drives both input formats (SQLite via `read_sqlite`, CSV/CSV.gz via `read_csv`) and deduplicates by MAC. `export_csv` / `export_kml` are also here. The `Hit` dataclass computes `confidence` (1–3) from its `signals` list — signals are `(label, detail)` tuples appended by `analyze()`.

**`enrich.py`** — post-detection enrichment, no UI imports. Three `Enricher` subclasses mutate `Hit.signals` in-place: `OverpassEnricher` (OSM ALPR nodes via DeFlock's Overpass mirror, free), `ALPRWatchEnricher` (downloads daily KMZ from alprwatch.org, caches 24 h, parses KML placemarks), `WiGLEEnricher` (looks up MACs via WiGLE API, requires key). `build_enrichers()` assembles the list; `enrich_hits(hits, enrichers, callback)` runs them with per-hit progress callbacks. WiGLE credentials stored in `~/.config/flock-wigle/config.json` or env vars `WIGLE_API_NAME`/`WIGLE_API_TOKEN`.

**`tui.py`** — Textual app. `FlockDetectApp` loads data via `@work(thread=True)` so the UI stays responsive. `FilterPanel` checkboxes (including "Group nearby") drive `_rebuild_table()`, which converts filtered hits into `list[Cluster]` via either `detect.cluster_hits()` or `detect.single_clusters()`. `DataTable.RowHighlighted` updates `DetailPanel`. Key bindings: `m`/`v`/`c` (Maps/StreetView/copy MAC), `n` (enrich), `d`/`D` (discover/force-refresh), `o` (open OSM iD editor + copy tags), `w` (WiGLE config), `e`/`k`/`g` (CSV/KML/GeoJSON export).

**`discover.py`** — WiGLE discovery with disk cache. `WiGLEDiscovery.discover()` fetches BLE name + WiFi SSID results from WiGLE, converts to `Hit` objects via `analyze()`, and caches to `~/.cache/flock-wigle/wigle-discovery.json`. Cache is `partial=True` if rate-limited mid-run — served regardless of TTL with a retry hint. `save_cache()` is called after each individual query so work is never fully lost on 429.

**`serial_import.py`** — no UI imports. Ingests the flockdar-esp32 JSON stream from a live serial port (`serial_lines`, lazy `pyserial`) or a saved NDJSON log (`log_lines`, the SD-card file). `verify_line()` checks the HMAC of `wifi`/`ble` lines by stripping the trailing `,"sig":"<hex>"` to reconstruct the signed bytes; `gps` lines update a running position that detections inherit. `iter_records`/`iter_hits` map each line to a `detect` Record / `Hit` (adding an `ESP32_LIVE` provenance signal). `merge_hit()` dedups by MAC; `load_log()` returns deduped hits; `write_sqlite()` emits a WiGLE-format `network` table so output is re-openable by the TUI. CLI: `flockdar-ingest <port|log> [out.sqlite]` (= `python -m flockdar.serial_import`). HMAC key from `--key`, `$FLOCKDAR_HMAC_KEY`, or the firmware default. The TUI consumes this in `--serial` live mode and when opening `.ndjson`/`.jsonl`/`.log` files.

**`esp32/`** — Working PlatformIO firmware for the ESP32 companion scanner. Scanners (`wifi_scanner` promiscuous, `ble_scanner` NimBLE) push `Detection` records onto a FreeRTOS queue; the main loop drains it and `serial_out` serialises + HMAC-signs each line to USB serial and (with `FD_ENABLE_SD`) the microSD log. `match.cpp` checks OUI/mfgrid/BLE-name against the generated `oui_list.h`. Optional `gps`/`display`/`sdlog` are compile-guarded (`FD_ENABLE_GPS`/`_OLED`/`_SD`). T-Deck Plus GPS auto-detects **L76K @ 9600** vs **u-blox M10 @ 38400** (`esp32/src/gps.cpp`; see `esp32/BOARDS.md`). `gen_oui_header.py` regenerates `oui_list.h` (OUIs, mfgrids, **and** BLE name patterns) from `signatures.py` so Python and C stay in sync — re-run after editing signatures. GPIO assignments are NOT hand-written: `pin_spec.py` is the validated source of truth (chip reserved-pin DB + per-board maps) that generates `src/pins.h` (board-conditional via `CONFIG_IDF_TARGET_*`); `config.h` includes it and holds behaviour knobs only. Run `pin_spec.py validate` (errors fail) then `gen`; `tests/test_pins.py` enforces both validity and header sync in CI. Hardware design is documented in `esp32/HARDWARE.md`. Build envs: `esp32-s3`, `esp32`, `esp32-s3-sd`, `esp32-s3-full`, `t-deck`. The serial/SD JSON protocol and signing scheme are documented in `esp32/README.md`. Host tools: `flockdar-ingest --monitor`, `--sd-list`, `--sd-dump`, `--gps-summary`; serial commands `sd list|dump|abort`, `gps tap`.

## Input formats

The tool reads two WiGLE data formats:

- **SQLite** (`.sqlite`/`.db`) — WiGLE Android app backup; richer, includes `service` column with BLE service UUIDs space-separated
- **CSV.gz** (`.csv.gz`/`.csv`) — WiGLE web export or app export; no service UUID data

The SQLite `network` table has a single-char `type` field: `W`=WiFi, `E`=BLE, `B`=Bluetooth Classic.

## Confidence scoring

`Hit.confidence` is derived from signal labels (not stored explicitly):

- **3 HIGH**: `FLOCK_DIRECT_OUI`, `RAVEN_UUID_HIGH`, `FLOCKNET_SSID`, `FLOCK_CAMERA_SSID`, `PENGUIN_BLE_SSID`
- **2 MEDIUM**: `FLOCK_CAMERA_SSID_PATTERN`, `BLE_NAME`, `FLOCK_WIFI_FP`, `FLOCK_MFGRID`
- **1 LOW**: `CHIP_OUI`, `SSID_PATTERN`, `RAVEN_UUID_OLD`, `SURVEILLANCE_OUI`, `BACKHAUL_OUI_HIDDEN`

> **README diagram** — `README.md` has a Mermaid confidence-tier diagram in the **How detection works** section. Update it whenever a signal label is added, removed, or moved to a different tier.

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
