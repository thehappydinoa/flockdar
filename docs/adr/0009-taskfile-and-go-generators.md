# ADR-0009: Taskfile as build system, Go generators replacing Python tooling

**Status:** Accepted  
**Date:** 2026-06-04

## Context

The prototype uses Python tooling scripts (`gen_oui_header.py`, `pin_spec.py`) to generate C++ headers from Python signature data. The host-side build uses `uv` / `pyproject.toml`. As the project moves to Go, minimising Python in the build chain reduces toolchain complexity and cognitive overhead for contributors.

Two candidates for a task runner: `make` and `task` (Taskfile). Python tooling scripts have direct Go equivalents.

## Decision

**`task` (Taskfile.dev)** as the project-wide task runner. **Go programs** replace all Python tooling scripts. Python is removed from the build chain entirely; it remains only as an implicit dependency of PlatformIO (which we do not control).

### Taskfile over Make

| | Make | Taskfile |
|---|---|---|
| Syntax | POSIX shell, fragile whitespace rules | YAML, explicit |
| Cross-platform | Poor on Windows | Good |
| Dependencies | GNU Make assumed | Single Go binary |
| Parallel tasks | Manual | `--parallel` flag |
| Documentation | Convention only | `desc:` field, `task --list` |
| Installation | Usually pre-installed | `go install` or brew/apt |

Taskfile is a Go binary (`task`), consistent with the project's Go-first toolchain.

### Go generators over Python scripts

| Python script | Go replacement | Output |
|---|---|---|
| `gen_oui_header.py` | `cmd/gen-oui/main.go` | `esp32/src/oui_list.h` + `internal/detect/signatures_gen.go` |
| `pin_spec.py validate` | `cmd/gen-pins/main.go validate` | exit code (CI) |
| `pin_spec.py gen` | `cmd/gen-pins/main.go gen` | `esp32/src/pins.h` |

Both generators read from `signatures.toml` and `pinspec.toml` respectively (see ADR-0010). No Python interpreter required at any point in the build.

### Python elimination scope

| Component | Python before | After |
|---|---|---|
| Host CLI/TUI | `uv run flockdar` | `task run` → Go binary |
| Tests | `uv run pytest` | `task test` → `go test ./...` |
| OUI header gen | `uv run gen_oui_header.py` | `task gen:oui` → `go run ./cmd/gen-oui` |
| Pin header gen | `uv run pin_spec.py gen` | `task gen:pins` → `go run ./cmd/gen-pins` |
| PlatformIO build | `pio run` (Python internal) | `task firmware:build` → `pio run` (unchanged) |

PlatformIO requires Python internally but this is a tool dependency — not project code. Contributors install PlatformIO once; the Taskfile invokes it.

## Taskfile structure

```yaml
# Taskfile.yml (root)
version: '3'

tasks:
  # --- codegen ---
  gen:oui:
    desc: "Generate OUI header (esp32) and Go signatures from signatures.toml"
    sources: [signatures.toml]
    generates: [esp32/src/oui_list.h, internal/detect/signatures_gen.go]
    cmds:
      - go run ./cmd/gen-oui signatures.toml esp32/src/oui_list.h internal/detect/signatures_gen.go

  gen:pins:
    desc: "Validate pinspec and regenerate esp32/src/pins.h"
    sources: [pinspec.toml]
    generates: [esp32/src/pins.h]
    cmds:
      - go run ./cmd/gen-pins gen pinspec.toml esp32/src/pins.h

  gen:
    desc: "Run all code generators"
    deps: [gen:oui, gen:pins]

  # --- build ---
  build:
    desc: "Build all Go binaries"
    cmds:
      - go build -o bin/muninnd ./cmd/muninnd
      - go build -o bin/flockdar  ./cmd/flockdar

  build:arm64:
    desc: "Cross-compile for Raspberry Pi (linux/arm64)"
    env:
      GOOS: linux
      GOARCH: arm64
    cmds:
      - go build -o bin/muninnd-arm64 ./cmd/muninnd
      - go build -o bin/flockdar-arm64  ./cmd/flockdar

  # --- test ---
  test:
    desc: "Run Go tests"
    cmds:
      - go test ./...

  test:race:
    desc: "Run Go tests with race detector"
    cmds:
      - go test -race ./...

  lint:
    desc: "Run staticcheck and go vet"
    cmds:
      - go vet ./...
      - staticcheck ./...

  # --- firmware ---
  firmware:build:
    desc: "Build ESP32 firmware (T-Deck)"
    dir: esp32
    cmds:
      - pio run -e t-deck

  firmware:flash:
    desc: "Flash T-Deck firmware"
    dir: esp32
    cmds:
      - pio run -e t-deck -t upload

  firmware:build:s3:
    desc: "Build ESP32-S3 firmware"
    dir: esp32
    cmds:
      - pio run -e esp32-s3

  firmware:flash:s3:
    desc: "Flash ESP32-S3 firmware"
    dir: esp32
    cmds:
      - pio run -e esp32-s3 -t upload

  # --- dev ---
  dev:
    desc: "Run daemon in dev mode (serial + web UI on :8080)"
    cmds:
      - go run ./cmd/muninnd --dev --module serial:{{.PORT | default "/dev/ttyUSB0"}}

  install:
    desc: "Install muninnd as systemd service"
    cmds:
      - task: build:arm64
      - scp bin/muninnd-arm64 {{.TARGET}}:/usr/local/bin/muninnd
      - ssh {{.TARGET}} systemctl restart muninnd
    vars:
      TARGET: '{{.TARGET | default "pi-pelican.local"}}'

  # --- ci ---
  ci:
    desc: "Full CI check (gen + build + test + lint + firmware build)"
    cmds:
      - task: gen
      - task: build
      - task: test:race
      - task: lint
      - task: firmware:build
```

## Developer setup (after this ADR)

```bash
# Install task runner (once)
go install github.com/go-task/task/v3/cmd/task@latest

# Install PlatformIO (once, only needed for firmware work)
pip install platformio   # or brew install platformio

# Everything else
task gen        # generate all headers
task build      # build Go binaries
task test       # run tests
task firmware:build  # build ESP32 firmware
task ci         # full CI check locally
```

No `uv`, no `virtualenv`, no `pyproject.toml` in the new repository.

## Consequences

- `signatures.toml` replaces `signatures.py` as canonical signature data (ADR-0010)
- `pinspec.toml` replaces the embedded data in `pin_spec.py`
- `cmd/gen-oui/` and `cmd/gen-pins/` are first Go programs written in the new repo
- CI uses `task ci` as the single entrypoint
- Contributors need: Go toolchain + `task` + PlatformIO (firmware only)
- Python dependency eliminated from project tooling; PlatformIO's internal Python is a tool concern, not ours
