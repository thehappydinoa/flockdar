# ADR-0010: signatures.toml as canonical signature data source

**Status:** Accepted  
**Date:** 2026-06-04

## Context

Detection signatures (OUI prefixes, BLE service UUIDs, SSID patterns, confidence tiers) currently live in `signatures.py`. This file is simultaneously data and Python code. The Go generator (`cmd/gen-oui`) and the Go detection engine (`internal/detect`) need to read this data without a Python interpreter.

## Decision

Move all signature data to **`signatures.toml`** — a language-agnostic, human-readable data file at the repository root. `signatures.py` in the legacy Python repo is retired as the source of truth.

`cmd/gen-oui` reads `signatures.toml` and generates:
- `esp32/src/oui_list.h` — C++ header for firmware matching
- `internal/detect/signatures_gen.go` — Go package for daemon detection

Both generated files are committed to the repository and checked in CI (`task gen && git diff --exit-code`).

## Format

```toml
# signatures.toml

[meta]
version = 3
updated = "2026-06-04"

# OUIs that are definitively Flock Safety hardware
[[oui.direct]]
prefix     = "b4:a5:ef"
label      = "FLOCK_DIRECT_OUI"
confidence = "high"
notes      = "Flock Safety registered OUI"

[[oui.direct]]
prefix     = "d8:3a:dd"
label      = "FLOCK_DIRECT_OUI"
confidence = "high"

# OUIs for chips used in Flock cameras (ESP32, etc.)
[[oui.chip]]
prefix     = "84:f7:03"
label      = "CHIP_OUI"
confidence = "low"
notes      = "Espressif ESP32-S"

# Backhaul OUIs (eero, etc.) — only flagged when SSID is hidden
[[oui.backhaul]]
prefix     = "80:da:13"
label      = "BACKHAUL_OUI_HIDDEN"
confidence = "low"
notes      = "eero — flagged only on blank SSID"
condition  = "hidden_ssid"

# Surveillance vendor OUIs (non-Flock but notable)
[[oui.surveillance]]
prefix     = "00:1a:07"
label      = "SURVEILLANCE_OUI"
confidence = "low"
vendor     = "Axis Communications"

# BLE service UUIDs
[[ble.service]]
uuid       = "0000fea0-0000-1000-8000-00805f9b34fb"
label      = "RAVEN_UUID_HIGH"
confidence = "high"

[[ble.service]]
uuid       = "0000fe9a-0000-1000-8000-00805f9b34fb"
label      = "RAVEN_UUID_OLD"
confidence = "low"

# BLE name patterns (regex)
[[ble.name]]
pattern    = "^Penguin-\\d{10}$"
label      = "PENGUIN_BLE_SSID"
confidence = "high"

[[ble.name]]
pattern    = "^FS Ext Battery"
label      = "BLE_NAME"
confidence = "medium"

# BLE manufacturer IDs
[[ble.manufacturer]]
id         = 2504
label      = "FLOCK_MFGRID"
confidence = "medium"
notes      = "Penguin manufacturer ID"

# SSID patterns (regex)
[[ssid]]
pattern    = "^[Ff][Ll][Oo][Cc][Kk]-[0-9A-Fa-f]{6}$"
label      = "FLOCK_CAMERA_SSID"
confidence = "high"
notes      = "Validated: suffix matches last 6 MAC chars OR OUI in chip_ouis"

[[ssid]]
pattern    = "^FLOCKNET-"
label      = "FLOCKNET_SSID"
confidence = "high"

# WiFi capability fingerprint
[[wifi.fingerprint]]
label        = "FLOCK_WIFI_FP"
confidence   = "medium"
capabilities = "[WPA2-PSK-CCMP-128][RSN-PSK-CCMP-128][ESS]"
frequencies  = [2412, 2437, 2462]
oui_group    = "chip"
notes        = "Sleeping camera: hidden SSID + chip OUI + known caps + 2.4GHz channel"
```

## Why TOML over JSON or YAML

- **TOML over JSON:** TOML supports comments — essential for a security research data file where provenance and notes matter. JSON does not.
- **TOML over YAML:** TOML has no indentation-sensitive parsing, no implicit type coercion, no `Norway problem`. Multi-line arrays are unambiguous.
- **TOML over CSV:** Heterogeneous record types (OUI, BLE name, SSID pattern) don't fit a flat CSV schema.

## Generated file policy

Generated files (`oui_list.h`, `signatures_gen.go`) are **committed to the repository**:

- Firmware builds in CI without requiring Go toolchain
- Code review shows exact signature changes in the generated output
- `task gen && git diff --exit-code` in CI enforces that committed generated files match the source

## Adding a new signature

```bash
# 1. Edit signatures.toml
# 2. Regenerate
task gen:oui
# 3. Review generated diff
git diff esp32/src/oui_list.h internal/detect/signatures_gen.go
# 4. Commit all three files together
git add signatures.toml esp32/src/oui_list.h internal/detect/signatures_gen.go
git commit -m "signatures: add OUI b4:a5:ef (Flock Safety direct)"
```

## Migration from signatures.py

A one-time migration script (`cmd/migrate-sigs/main.go`) reads `signatures.py` via text parsing (not import) and writes `signatures.toml`. After migration, `signatures.py` is archived in the legacy Python repo and no longer maintained.

## Consequences

- `signatures.toml` is the single source of truth for all detection signatures
- No Python required to update signatures or rebuild headers
- `cmd/gen-oui` is the second Go program written in the new repo (after `cmd/gen-pins`)
- Community signature contributions are PRs to `signatures.toml` — readable without any programming knowledge
- `task gen:oui` must be run and output committed after any signature change
