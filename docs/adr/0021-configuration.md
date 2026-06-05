# ADR-0021: TOML configuration with environment variable and flag overrides

**Status:** Accepted  
**Date:** 2026-06-04

## Decision

**TOML** as the configuration file format. Three-layer override hierarchy with later layers winning:

```
config.toml  →  environment variables  →  CLI flags
```

This follows the principle of least surprise: the file is the baseline, env vars allow container/CI overrides without editing the file, flags allow one-off overrides without changing anything persistent.

## Full configuration reference

```toml
# /etc/muninn/config.toml

# ── Node identity ─────────────────────────────────────────────────────────────
[node]
id   = "pi-pelican"          # stable unique identifier — used in protocol records
name = "Pelican Hub"         # human-readable display name

# ── Modules ───────────────────────────────────────────────────────────────────
[modules]
enabled = ["coconut", "wifi", "ble", "meshtastic"]
# available: coconut, wifi, ble, meshtastic, serial

[modules.coconut]
# auto-detected by USB VID/PID — no config required
binary = "wifi_coconut"      # path to wifi_coconut binary if not in $PATH

[modules.wifi]
interface = "wlan1"          # monitor mode interface (Alfa adapter)

[modules.ble]
adapter   = "hci1"           # BlueZ adapter (hci0 = built-in, hci1 = USB dongle)

[modules.meshtastic]
port    = "/dev/ttyUSB0"     # H2T serial port
channel = "flockdar"
key     = ""                 # 32-byte hex PSK — required if channel != PRIMARY

[modules.serial]
port    = "/dev/ttyUSB1"     # USB-attached ESP32 (separate from H2T)
baud    = 115200

# ── API ───────────────────────────────────────────────────────────────────────
[api]
listen_external  = "0.0.0.0:8443"    # bearer token + TLS
listen_internal  = "127.0.0.1:8081"  # no auth, LAN sync endpoint only
tls_cert         = "/var/lib/muninn/tls/cert.pem"
tls_key          = "/var/lib/muninn/tls/key.pem"

# ── Auth ──────────────────────────────────────────────────────────────────────
[auth]
token = ""   # 32-byte base64url token — generated on first run if empty

# ── Storage ───────────────────────────────────────────────────────────────────
[store]
path       = "/var/lib/muninn/detections.db"
cache_mb   = 32          # SQLite page cache size

# ── Tiles ─────────────────────────────────────────────────────────────────────
[tiles]
path     = "/var/lib/muninn/tiles.pmtiles"  # omit to disable offline tiles
max_zoom = 16

# ── Signatures ────────────────────────────────────────────────────────────────
[signatures]
path         = "/var/lib/muninn/signatures.toml"  # optional override
feed_url     = ""           # leave empty to disable auto-pull
auto_pull    = false
pull_interval = "24h"

# ── Logging ───────────────────────────────────────────────────────────────────
[logging]
level  = "info"    # trace|debug|info|warn|error
format = "auto"    # auto|json|text

# ── HMAC keys (per-node) ──────────────────────────────────────────────────────
# Maps node_id → 32-byte hex HMAC key
# The daemon rejects records with unknown node_ids unless allow_unknown = true
[hmac]
allow_unknown = false
keys = {
  "t-deck-van"  = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
  "pi-zero-bag" = "cafecafecafecafecafecafecafecafecafecafecafecafecafecafecafecafe",
}
```

## Environment variable overrides

Every config key maps to an env var: `MUNINN_` prefix + section + key in `SCREAMING_SNAKE_CASE`:

| Config key | Env var |
|---|---|
| `node.id` | `MUNINN_NODE_ID` |
| `auth.token` | `MUNINN_AUTH_TOKEN` |
| `store.path` | `MUNINN_STORE_PATH` |
| `logging.level` | `MUNINN_LOG_LEVEL` |
| `modules.meshtastic.key` | `MUNINN_MODULES_MESHTASTIC_KEY` |

Env vars are useful in systemd unit override files:

```ini
# /etc/systemd/system/muninnd.service.d/override.conf
[Service]
Environment=MUNINN_LOG_LEVEL=debug
Environment=MUNINN_AUTH_TOKEN=my-token
```

## CLI flags

Flags override both file and env vars. Available flags:

```
muninnd --config /path/to/config.toml
          --dev                           # dev mode: no TLS, no auth, localhost only
          --module wifi:wlan0             # add/override module at runtime
          --log-level debug
          --listen :8080
```

`--module` flags are additive: `--module coconut --module serial:/dev/ttyUSB0` enables those two modules regardless of what `config.toml` says.

## First-run defaults

If `config.toml` doesn't exist, the daemon runs with safe defaults:

```
node.id    = hostname
auth.token = <generated 32-byte random, printed to stdout, written to config>
tls cert   = <generated self-signed, written to /var/lib/muninn/tls/>
modules    = [] (no modules — web UI and API only)
store.path = /var/lib/muninn/detections.db
logging    = info, auto format
```

This lets you `muninnd` with no config and immediately access the web UI to configure it interactively (future).

## Config validation

On startup, the daemon validates the full config and exits with clear errors before starting any module:

```
FATAL config: modules.meshtastic.key is required when channel != "PRIMARY"
FATAL config: modules.wifi.interface "wlan5" does not exist
FATAL config: auth.token too short (must be >= 32 bytes)
FATAL config: hmac.keys["t-deck-van"] is not valid hex
```

## Taskfile additions

```yaml
config:validate:
  desc: "Validate config file without starting daemon"
  cmds:
    - muninnd --config {{.CONFIG | default "/etc/muninn/config.toml"}} --validate-only

config:show:
  desc: "Print resolved config (with env vars applied, secrets redacted)"
  cmds:
    - muninnd --config {{.CONFIG | default "/etc/muninn/config.toml"}} --show-config
```

## Consequences

- `github.com/BurntSushi/toml` for TOML parsing (most mature Go TOML library)
- `github.com/caarlos0/env` for env var mapping (or manual mapping — small surface)
- Config struct is the single source of truth for all tunable parameters
- `--show-config` redacts `auth.token` and `hmac.keys` values (`[redacted]`)
- `--validate-only` exits 0 on valid config, 1 on error — useful in CI to validate config changes
