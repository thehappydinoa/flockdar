# ADR-0011: TypeScript + Vite for web UI

**Status:** Accepted  
**Date:** 2026-06-04

## Context

The embedded web UI needs to handle a live WebSocket hit stream, a Leaflet map with thousands of markers, run history tables, and a node status dashboard. Vanilla JS works for trivial UIs but becomes hard to maintain at this complexity. A TypeScript build step adds tooling but pays for itself quickly.

## Decision

**TypeScript + Vite** for the web UI. No framework — typed DOM, fetch, and WebSocket APIs directly. Built output embedded in the Go binary via `//go:embed web/dist`.

| Choice | Rationale |
|---|---|
| TypeScript | Type safety on API responses, WebSocket message shapes, Leaflet calls. Catches API contract drift at build time. |
| Vite | Fast HMR dev server, trivial TS config, outputs a single `dist/` directory. No webpack config to maintain. |
| No framework | The UI is a map + live table + status panel — not a component tree. React/Vue adds bundle weight and complexity for no real benefit here. |
| Leaflet | Best-in-class open source map library, `@types/leaflet` available, works offline with tile caching. |
| No CSS framework | Plain CSS variables, minimal. The UI is a tool, not a product. |

## Dev workflow

```bash
# Terminal 1: Go daemon (API + WebSocket)
task dev

# Terminal 2: Vite dev server (proxies /api and /ws to Go daemon)
task web:dev
# Opens http://localhost:5173 with HMR
```

Vite proxy config (`web/vite.config.ts`):
```typescript
export default {
  server: {
    proxy: {
      '/api': 'http://localhost:8080',
      '/ws':  { target: 'ws://localhost:8080', ws: true }
    }
  }
}
```

## Production build + embed

```bash
task web:build
# Runs: cd web && vite build → web/dist/
# Go binary embeds web/dist via:
#   //go:embed web/dist
#   var webUI embed.FS
```

The daemon serves `web/dist/` as a static file handler. Single binary, no separate web server.

## TypeScript API contract

API response types are defined in `web/src/api.ts` and kept in sync with Go structs manually (or via a future `task gen:types` that emits TS from Go structs using `tygo` or similar).

```typescript
// web/src/api.ts
export interface Hit {
  id:         string
  mac:        string
  type:       'wifi' | 'ble'
  node_id:    string
  run_id:     string
  confidence: 1 | 2 | 3
  signals:    Signal[]
  lat?:       number
  lon?:       number
  rssi:       number
  ts:         number
  via:        string
  vendor?:    string      // from OUI DB
  device_type?: string   // from OUI DB
}

export interface Signal {
  label:  string
  detail: string
}

export interface Run {
  id:         string
  name:       string
  node_id:    string
  started_at: number
  ended_at?:  number
  hit_count:  number
  new_count:  number
  distance_m: number
}

export interface Node {
  id:           string
  name:         string
  last_seen:    number
  battery_pct?: number
  gps_fix:      boolean
  lat?:         number
  lon?:         number
  hit_count:    number
}

// WebSocket message envelope
export type WSMessage =
  | { type: 'hit';  data: Hit  }
  | { type: 'node'; data: Node }
  | { type: 'gap';  data: Gap  }
```

## Web directory layout

```
web/
  src/
    main.ts         — entry point, router
    api.ts          — typed API client + WS types
    map.ts          — Leaflet map, marker management
    hits.ts         — live hit feed table
    runs.ts         — run history view
    nodes.ts        — node status dashboard
    icons.ts        — device type → SVG icon mapping
  index.html
  vite.config.ts
  tsconfig.json
  package.json      — devDependencies only (vite, typescript, @types/leaflet)
```

## Taskfile additions

```yaml
web:dev:
  desc: "Start Vite dev server (proxied to running daemon)"
  dir: web
  cmds:
    - npm run dev

web:build:
  desc: "Build web UI into web/dist/ for embedding"
  dir: web
  cmds:
    - npm run build

web:install:
  desc: "Install web UI dependencies"
  dir: web
  cmds:
    - npm install
```

## Consequences

- `web/` directory added to repo with `package.json`, `tsconfig.json`, `vite.config.ts`
- `npm install` required once for web UI development (Node.js toolchain)
- `node_modules/` gitignored; `web/dist/` committed (so Go build works without Node.js)
- CI: `task web:build` added to `task ci` pipeline
- Contributors working on Go only never need Node.js — `web/dist/` is pre-built and committed
