# flockdar roadmap

---

## v0.2 — Live wardriving

Connect flockdar to a moving vehicle without waiting for WiGLE to sync.

- ✅ **flockdar-esp32 serial reader** — `serial_import.py` ingests the ESP32's signed JSON-over-serial stream in real time, verifying the HMAC and converting each detection to a `Hit` via `analyze()`. `uv run flockdar --serial /dev/ttyUSB0` feeds directly into the TUI; `.ndjson`/`.jsonl`/`.log` files (incl. SD-card logs) open natively.
- ✅ **Detection alerts** — live mode rings the bell and posts a notification when a new HIGH-confidence device appears.
- **flock-back import** — read `flocks.json` (the newline-delimited JSON output from [flock-back](https://github.com/NSM-Barii/flock-back)) as an additional data source alongside WiGLE exports.
- **Live SQLite watcher** — poll the WiGLE app's SQLite file for new rows every few seconds so the TUI updates as you drive, without manual reloads.

---

## v0.3 — Analytics

Turn the 7,900+ cameras in WiGLE into publishable findings.

- **Density map** — cameras per county/state using the cached discovery data, exported as GeoJSON for QGIS or geojson.io.
- **Temporal deployment chart** — camera `firsttime` dates plotted over time to show rollout acceleration by region.
- **Coverage gap analysis** — what percentage of WiGLE-discovered cameras are in OSM, ALPRWatch, or both; broken down by state.
- **FOIA cross-reference** — [MuckRock](https://muckrock.com) indexes public Flock Safety contracts by municipality. Flag cities where cameras are detected but no public contract exists, or where a contract exists but no cameras are found.

---

## v0.4 — flockdar-esp32 firmware

Build the actual firmware described in `esp32/README.md`. A standalone passive scanner that feeds directly into the flockdar pipeline.

- ✅ **WiFi promiscuous** — OUI-matched probe requests plus the addr1 receiver technique (catches sleeping cameras as destinations of nearby probe responses).
- ✅ **BLE scanner** — name match (`FS Ext Battery`, `Penguin-*`) and manufacturer ID 2504, using the auto-generated `oui_list.h`.
- ✅ **GPS tagging** — u-blox or MTK module; each detection gets lat/lon/accuracy in the JSON output.
- ✅ **OLED display** — live detection count, last MAC seen, current channel.
- ✅ **Signed JSON output** — HMAC-signed lines over USB serial so the Python receiver can reject forged or corrupted frames.
- ✅ **microSD logging** — untethered wardriving to `flock-NNNN.ndjson` on the card; replay through `serial_import` or open directly in the TUI.
- **OUI refresh** — on-boot fetch of a current `signatures.json` from a configurable URL so the device stays up to date without reflashing.

---

## v0.5 — Community contribution

Close the loop: detected camera → community database entry.

- **Batch OSM upload** — submit confirmed camera nodes to OpenStreetMap via the OAuth 2.0 API, not just opening the iD editor. Handles deduplication against existing nodes.
- **ALPRWatch submission** — flag cameras not yet in the ALPRWatch dataset via their submission workflow.
- **Community diff report** — weekly summary: cameras discovered since last run that aren't in any community database yet.

---

## v1.0 — Complete pipeline

End-to-end: ESP32 on the dashboard → USB → flockdar → OSM.

- **Route avoidance** — given a start and end point, suggest the route that minimises known camera encounters using the ALPRWatch KMZ avoidance data.
- **Nightly discovery** — scheduled mode that refreshes the WiGLE cache overnight and surfaces newly-logged cameras not yet in OSM.
- ✅ **Installable package** — src-layout `flockdar` package builds to a wheel with hatchling (`uv build`); `flockdar` / `flockdar-ingest` console scripts. Publish to PyPI so `uv tool install flockdar` / `pipx install flockdar` works without cloning.
- **Web dashboard** — optional `--serve 8080` that exposes a read-only Leaflet map of local detections for a second screen while driving.
