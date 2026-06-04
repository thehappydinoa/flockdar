# ADR-0002: New repository for production ecosystem

**Status:** Accepted  
**Date:** 2026-06-04

## Context

The current `flockdar` repository is a Python + C++ prototype that proved the core detection concept. The expanded vision — distributed multi-node ecosystem, Go daemon, web UI, Meshtastic integration, scanner modules — is architecturally different enough that continuing in the same repository creates significant friction:

- Python package tooling (`pyproject.toml`, `uv`, `hatchling`) conflicts with Go module layout
- The existing `src/flockdar/` structure would need to coexist with a `go/` or `cmd/` tree
- Git history and issues are scoped to the prototype; the new project deserves a clean start
- CI/CD pipelines, contribution guidelines, and release workflows differ substantially

## Decision

Start a **new repository** for the production Go ecosystem.

The existing `flockdar` Python repository is retained as:
- The reference prototype and canonical source of truth for detection signatures
- A working tool for users who want the Python TUI today
- The source from which `signatures.py` data is extracted for the Go implementation

## Repository structure (new repo)

```
cmd/
  flockdard/        — daemon binary (main entry point)
  flockdar/         — CLI binary (TUI, file analysis, one-shot tools)
internal/
  daemon/           — daemon core, module registry
  modules/
    wifi/           — monitor mode scanner (gopacket/libpcap)
    ble/            — BlueZ BLE scanner
    serial/         — USB serial ESP32 ingest
    meshtastic/     — Meshtastic GPS + mesh relay
    coconut/        — WiFi Coconut multi-interface handler
  detect/           — detection engine (port of signatures.py + detect.py)
  protocol/         — NDJSON wire format, HMAC verification
  store/            — SQLite backend
  api/              — REST + WebSocket handlers
  sync/             — loot queue, opportunistic sync
  runs/             — wardrive run tracking, GPS trace storage
web/                — embedded web UI (HTML + vanilla JS)
esp32/              — firmware (C++, PlatformIO) — copied from flockdar repo
  gen_oui_header.py — tooling script (Python, retained)
  pin_spec.py       — tooling script (Python, retained)
docs/
  adr/              — architecture decision records
  hardware/         — hardware build guides
```

## Naming

The new repository may use a different name to reflect the expanded scope. Options under consideration:

- `flockdar` — continuity, already known
- A new name that reflects the multi-platform ecosystem nature

This is an open decision and does not affect the technical architecture.

## Consequences

- Python `flockdar` package on PyPI is not deprecated; it continues to receive signature updates
- Signature data is maintained in `flockdar/signatures.py` and synced to the Go `internal/detect/` package via a generation step (similar to how `gen_oui_header.py` syncs to ESP32)
- ESP32 firmware lives in the new repo; the Python repo's `esp32/` directory becomes read-only reference
- CI in the new repo: `go test ./...`, `go vet`, `staticcheck`, plus ESP32 PlatformIO build check
