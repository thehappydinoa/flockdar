# ADR-0012: Full IEEE OUI database for vendor and device enrichment

**Status:** Accepted  
**Date:** 2026-06-04

## Context

The current signature set contains OUI prefixes only for known Flock/surveillance hardware. Every other MAC shows as "unknown vendor." With a Pi in the loop (adequate storage and compute), there is no reason to be stingy — the full IEEE OUI database gives vendor names, device categories, and icon hints for every detected MAC, making unknowns actionable and providing additional anomaly signals.

## Decision

Embed the full IEEE OUI database in the daemon's SQLite store. Update it via `task oui:update`. Use it to enrich every hit with `vendor` and `device_type` fields at ingest time.

## Database source

- **IEEE MA-L (MAC Address Large)**: 35,000+ 24-bit OUI assignments — `https://standards-oui.ieee.org/oui/oui.csv`
- **IEEE MA-M (MAC Address Medium)**: 28-bit assignments (smaller vendors) — `https://standards-oui.ieee.org/oui28/mam.csv`
- **IEEE MA-S (MAC Address Small)**: 36-bit assignments — `https://standards-oui.ieee.org/oui36/oui36.csv`
- **CID (Company ID)**: Bluetooth company identifiers — `https://standards-oui.ieee.org/cid/cid.csv`

Combined: ~55,000 entries, ~8MB in SQLite. Fetched and rebuilt by `task oui:update`.

## Device type classification

Vendor names are mapped to device types via a classification table in `signatures.toml`:

```toml
[[oui.vendor_class]]
pattern     = "^Apple"
device_type = "phone_tablet"
icon        = "device-phone"

[[oui.vendor_class]]
pattern     = "^Samsung"
device_type = "phone_tablet"
icon        = "device-phone"

[[oui.vendor_class]]
pattern     = "^(Espressif|ESP)"
device_type = "iot"
icon        = "device-iot"

[[oui.vendor_class]]
pattern     = "^(Axis|Hanwha|Hikvision|Dahua|Bosch Security)"
device_type = "camera"
icon        = "device-camera"
confidence_bump = 1   # surveillance vendor near road → bump confidence

[[oui.vendor_class]]
pattern     = "^(eero|Ubiquiti|Cisco|Netgear|TP-Link|ASUS)"
device_type = "networking"
icon        = "device-router"

[[oui.vendor_class]]
pattern     = "^(Flock Safety)"
device_type = "alpr"
icon        = "device-alpr"
```

`confidence_bump` allows the OUI DB to contribute to Flock detection — a CCTV vendor OUI near a road gets its confidence nudged up even without a Flock-specific signature match.

## Go implementation

```go
// internal/ouidb/ouidb.go
type Entry struct {
    Prefix     string  // "b4:a5:ef"
    Vendor     string  // "Flock Safety Inc."
    DeviceType string  // "alpr"
    Icon       string  // "device-alpr"
}

func (db *OUIDB) Lookup(mac string) *Entry
```

Prefix matching is done longest-prefix-first: MA-S (36-bit) beats MA-M (28-bit) beats MA-L (24-bit). Implemented as a sorted slice with binary search — fast enough at 55k entries with no index overhead.

## Taskfile

```yaml
oui:update:
  desc: "Fetch latest IEEE OUI databases and rebuild SQLite table"
  cmds:
    - go run ./cmd/oui-update

oui:stats:
  desc: "Show OUI database stats"
  cmds:
    - go run ./cmd/oui-update --stats
```

`cmd/oui-update` fetches the four IEEE CSVs, classifies vendors against `signatures.toml` patterns, and upserts into the `oui_db` SQLite table. Run on first install and periodically (quarterly — OUI assignments don't change often).

## Schema

```sql
CREATE TABLE oui_db (
    prefix      TEXT PRIMARY KEY,  -- "b4:a5:ef" or "b4:a5:ef:12" etc.
    prefix_bits INTEGER,           -- 24, 28, or 36
    vendor      TEXT,
    device_type TEXT,
    icon        TEXT,
    updated_at  INTEGER
);
```

## Web UI impact

Every hit marker on the Leaflet map uses `device_type` to select an SVG icon. Unknown vendors show a generic WiFi or BLE icon. Known CCTV vendors get a camera icon. Flock devices get the ALPR icon. This makes the map immediately readable without clicking individual markers.

The live hit feed table shows `vendor` next to the MAC, replacing the current blank column.

## Consequences

- `internal/ouidb/` package added
- `cmd/oui-update/` CLI added — run on install, not on every build
- SQLite `oui_db` table added to schema migration
- `task oui:update` added to `task install` (runs on first deploy, not on every update)
- `signatures.toml` gains `[[oui.vendor_class]]` section for device type mapping
- `Hit.Vendor` and `Hit.DeviceType` fields added — populated at ingest, stored in `networks` table
- ESP32 firmware unaffected — OUI enrichment is host-side only
