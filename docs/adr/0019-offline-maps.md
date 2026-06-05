# ADR-0019: Offline map tiles via PMTiles served by the daemon

**Status:** Accepted  
**Date:** 2026-06-04

## Context

The web UI uses Leaflet for the detection map. In field deployments the Pi has no internet access — tile requests to OpenStreetMap or any CDN fail. The map renders blank. This is unacceptable for a wardriving tool where the map *is* the primary output.

## Decision

Serve map tiles directly from the daemon binary using **PMTiles** format. Users download a regional `.pmtiles` file and drop it at a configured path. No separate tile server process.

## Why PMTiles

| Option | Process | Size | Complexity |
|---|---|---|---|
| PMTiles + go-pmtiles | None (daemon serves) | Regional ~1–5GB | Low |
| MBTiles + tileserver-gl | Node.js process | Regional ~1–5GB | Medium |
| MBTiles + martin | Rust process | Regional ~1–5GB | Low |
| Bundled raster tiles | None | Too large | N/A |
| Online only (OSM CDN) | None | Zero | Zero — fails offline |

PMTiles is a single-file, cloud-optimized tile archive format designed for HTTP range requests. The `go-pmtiles` library serves tiles from a local file with minimal code. No separate process, no port, no config beyond the file path.

Protomaps provides free PMTiles planet and regional extracts in both raster and vector formats.

## Implementation

```go
// internal/api/tiles.go
import "github.com/protomaps/go-pmtiles/pmtiles"

func (s *Server) mountTiles(mux *http.ServeMux, path string) error {
    loop, err := pmtiles.NewLoop(path, zerolog.Nop(), 64, "")
    if err != nil { return err }
    loop.Start()
    mux.HandleFunc("/tiles/", func(w http.ResponseWriter, r *http.Request) {
        // parse /{z}/{x}/{y}.{ext} from path
        // serve from pmtiles loop
        // set Cache-Control: max-age=86400
    })
    return nil
}
```

Tiles served at `/tiles/{z}/{x}/{y}.mvt` (vector) or `.png` (raster depending on extract).

If no `.pmtiles` file is configured, the daemon falls back to online OSM tiles — web UI shows a notice: "Offline tiles not configured. Using online tiles (requires internet)."

## Leaflet configuration

```typescript
// web/src/map.ts
const tileUrl = offlineTilesAvailable
  ? '/tiles/{z}/{x}/{y}.mvt'   // local daemon
  : 'https://tile.openstreetmap.org/{z}/{x}/{y}.png'  // fallback

L.tileLayer(tileUrl, {
  attribution: offlineTilesAvailable
    ? '© OpenStreetMap contributors (offline)'
    : '© OpenStreetMap contributors',
  maxZoom: 19,
}).addTo(map)
```

The daemon exposes `GET /api/v1/stats` which includes `"tiles": {"available": true, "source": "offline"}` — the web UI checks this on load to select the correct tile source.

## Tile download workflow

```bash
# Download US extract (~3GB vector tiles, covers all of contiguous US)
task tiles:download REGION=us

# Or a specific state/metro area (~50–500MB)
task tiles:download REGION=new-york

# Or the full planet (~100GB — not recommended for Pi SD card)
task tiles:download REGION=planet
```

```yaml
# Taskfile.yml
tiles:download:
  desc: "Download PMTiles extract for a region (REGION=us|new-york|etc)"
  vars:
    TILES_PATH: '{{.TILES_PATH | default "/var/lib/muninn/tiles.pmtiles"}}'
  cmds:
    - go run ./cmd/tiles-download --region {{.REGION}} --output {{.TILES_PATH}}

tiles:info:
  desc: "Show info about the current tiles file"
  cmds:
    - go run ./cmd/tiles-download --info {{.TILES_PATH | default "/var/lib/muninn/tiles.pmtiles"}}
```

`cmd/tiles-download` fetches from Protomaps' public builds at `https://build.protomaps.com/`. Regional extracts are derived from OpenStreetMap data.

## Storage recommendations

| Region | Approx size | Pi 4 fit? |
|---|---|---|
| Single metro (NYC, LA, etc.) | 50–200MB | Yes, any SD |
| Single US state | 200–800MB | Yes, any SD |
| Contiguous US | ~3GB | Yes, 32GB+ SD |
| Western Europe | ~5GB | Yes, 32GB+ SD |
| Planet | ~100GB | No — external SSD required |

For the Pelican Hub with a 128GB microSD, a full US extract is practical.

## Configuration

```toml
[tiles]
path = "/var/lib/muninn/tiles.pmtiles"  # omit to disable offline tiles
max_zoom = 16   # cap zoom to reduce tile requests at full zoom
```

## Consequences

- `go-pmtiles` library added as dependency
- `cmd/tiles-download` CLI added — downloads regional PMTiles from Protomaps
- Daemon serves tiles at `/tiles/*` alongside API — same port, same TLS cert
- Tiles endpoint requires bearer token auth (same as API)
- `task tiles:download REGION=us` documented in setup guides
- Leaflet configured to auto-detect offline vs online tile source via `/api/v1/stats`
- No separate tile server process — zero extra memory/CPU overhead vs. a separate service
