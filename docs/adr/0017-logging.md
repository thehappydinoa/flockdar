# ADR-0017: Structured logging with zerolog and systemd journal

**Status:** Accepted  
**Date:** 2026-06-04

## Decision

**zerolog** for structured logging. Output format adapts to context: JSON to stdout when running under systemd, human-readable colored text in dev mode. Log levels: TRACE, DEBUG, INFO, WARN, ERROR, FATAL.

## Why zerolog

| Library | Allocs | Format | Levels | Notes |
|---|---|---|---|---|
| `zerolog` | Zero | JSON / text | Full | Best performance, chained API |
| `slog` (stdlib) | Low | JSON / text | 4 built-in | Go 1.21+, fewer features |
| `zap` | Near-zero | JSON | Full | More complex API |
| `logrus` | High | JSON / text | Full | Older, not recommended for new code |

zerolog's zero-allocation design is appropriate for a daemon processing packets at high rate. The chained API is clean and expressive.

## Log levels

| Level | Use |
|---|---|
| `TRACE` | Per-packet details (raw frame bytes, HMAC computation) — never in production |
| `DEBUG` | Per-detection details, module state transitions, connection events |
| `INFO` | Normal operation events (daemon start, module loaded, run started, hit detected) |
| `WARN` | Recoverable issues (HMAC mismatch, GPS fix lost, module reconnecting) |
| `ERROR` | Non-fatal failures (module crashed and restarting, sync failed) |
| `FATAL` | Unrecoverable — daemon exits after logging (config parse failure, port bind failure) |

Default level in production: `INFO`. Set via config or env var.

## systemd journal integration

When running under systemd, the daemon writes JSON to stdout. systemd captures it in the journal with the process metadata (unit name, PID, timestamp) already added — no need to include them in the log line.

zerolog output under systemd:
```json
{"level":"info","module":"coconut","msg":"started capture","interfaces":14,"ts":"2026-06-04T12:00:00Z"}
{"level":"info","module":"meshtastic","msg":"GPS fix acquired","lat":40.001,"lon":-74.002,"accuracy":3.1}
{"level":"warn","module":"serial","port":"/dev/ttyUSB1","msg":"reconnecting","attempt":2,"backoff_ms":2000}
{"level":"info","module":"detect","mac":"b4:a5:ef:12:34:56","confidence":3,"signal":"FLOCK_DIRECT_OUI","msg":"hit"}
```

Journal queries:
```bash
# All daemon logs
journalctl -fu flockdard

# Errors only
journalctl -u flockdard -p err

# Hits only (JSON filtering via jq)
journalctl -u flockdard -o json | jq 'select(.msg=="hit")'

# Module-specific
journalctl -u flockdard | grep '"module":"coconut"'
```

## Dev mode format

When `--dev` flag is set or stdout is a TTY, zerolog uses `ConsoleWriter` for human-readable output:

```
12:00:00 INF coconut: started capture interfaces=14
12:00:01 INF meshtastic: GPS fix acquired lat=40.001 lon=-74.002 accuracy=3.1
12:00:05 WRN serial: reconnecting port=/dev/ttyUSB1 attempt=2 backoff_ms=2000
12:00:07 INF detect: hit mac=b4:a5:ef:12:34:56 confidence=3 signal=FLOCK_DIRECT_OUI
```

## Configuration

```toml
[logging]
level  = "info"    # trace|debug|info|warn|error
format = "auto"    # auto|json|text (auto = json if not TTY)
```

Environment override: `FLOCKDARD_LOG_LEVEL=debug`

## Sensitive field handling

The following fields are **never logged** at any level:
- Bearer token
- HMAC keys
- Node PSK (Meshtastic channel key)
- Full MAC addresses at TRACE level (last 3 octets only: `b4:a5:ef:**:**:**`)

## Module logger pattern

Each module gets a logger with its name pre-attached:

```go
log := zerolog.Ctx(ctx).With().Str("module", "coconut").Logger()
log.Info().Int("interfaces", 14).Msg("started capture")
log.Error().Err(err).Msg("subprocess exited unexpectedly")
```

## Consequences

- `zerolog` added as dependency
- Dev mode detected by `isatty` check on stdout
- No log files — systemd journal is the log sink; `journalctl` is the reader
- `task logs TARGET=pi` added to Taskfile: `ssh pi journalctl -fu flockdard`
- TRACE level requires explicit opt-in — never default, too verbose for packet capture rates
