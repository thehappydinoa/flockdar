# ADR-0001: Go as primary implementation language

**Status:** Accepted  
**Date:** 2026-06-04

## Context

The current flockdar prototype is split across Python (host CLI/TUI) and C++ (ESP32 firmware). As the project expands to a distributed multi-node ecosystem — daemon, web UI, scanner modules, mesh relay, sync — the host-side language choice has significant consequences for deployment, performance, and maintainability.

Candidates evaluated:

- **Python** — current implementation, rapid iteration, rich ecosystem
- **Go** — single binary, native concurrency, cross-compilation, strong networking primitives
- **Rust** — maximum performance, memory safety, potential ESP32 target via Embassy

## Decision

**Go** for all host-side components (daemon, scanner modules, CLI, web UI server).

**Python** retained for ESP32 tooling scripts (`gen_oui_header.py`, `pin_spec.py`) — these are build tools, not runtime components, and rewriting them offers no benefit.

**C++ retained for ESP32 firmware** — see ADR-0006.

## Rationale

### Go wins on deployment

A single statically-linked binary copies to a Raspberry Pi Zero 2W via `scp` and runs. No runtime, no virtualenv, no `uv sync`. Cross-compilation is `GOOS=linux GOARCH=arm64 go build`. This matters enormously for a field device that needs to be updated quickly.

### Go wins on concurrency model

The daemon runs many concurrent tasks: WiFi packet capture, BLE scanning, WebSocket server, REST API, Meshtastic serial reader, sync queue, GPS position updates. Go's goroutines and channels map naturally to this — each module is a goroutine feeding a shared detection channel. Python asyncio can do this but is harder to reason about under load.

### Go wins on operational tooling

- `gopacket` — mature packet capture library (wraps libpcap), well-tested for monitor mode
- `axum`-equivalent: `net/http` + `nhooyr.io/websocket` for WebSocket
- `bubbletea` — TUI framework, arguably better than Textual for this use case
- `meshtastic-go` — Meshtastic protobuf protocol library exists and is maintained
- Single binary means systemd unit files are trivial

### Rust trade-offs

Rust would deliver better raw performance and could eventually share code with ESP32 firmware via `no_std` crates and Embassy. However:
- Slower iteration velocity during the foundation phase
- Embassy on ESP32 is not yet production-ready for the full feature set (BLE + WiFi + SD + GPS simultaneously)
- The performance ceiling of Go is sufficient for the packet rates involved (Flock cameras are sparse, not firehose traffic)
- Rust remains the right answer if Go becomes a bottleneck; migration is feasible since the wire protocol is language-agnostic

### Python trade-offs

Python stays as prototype/reference. The detection logic (`signatures.py`, `detect.py`) is the canonical source of truth for signature data — the Go implementation derives from it (or the signatures are extracted to a shared data format, see ADR-0003).

## Consequences

- New repository bootstrapped as a Go module (see ADR-0002)
- ESP32 tooling scripts (`gen_oui_header.py`, `pin_spec.py`) copied to new repo, unchanged
- Python `flockdar` package remains as the reference prototype; not deleted
- Go implementation targets `linux/arm64` (Pi 4, Pi Zero 2W) and `linux/amd64` (laptop/server) as primary platforms
- Android (future) will be a separate Go or Kotlin/Flutter project consuming the daemon's REST + WebSocket API
