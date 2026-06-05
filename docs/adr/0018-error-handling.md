# ADR-0018: Error handling and resilience

**Status:** Accepted  
**Date:** 2026-06-04

## Principles

1. **A module failure never kills the daemon.** Scanner modules, serial connections, and Meshtastic serial are all externally-dependent — hardware disconnects, processes crash. The daemon continues serving the web UI and accepting data from healthy modules.
2. **Panics are recovered, not propagated.** Every goroutine has a deferred panic recovery that logs the stack trace and triggers module restart.
3. **External resources retry with exponential backoff.** Serial ports, subprocesses, Meshtastic connections all reconnect automatically.
4. **Detection data is never silently dropped.** If a module's send channel is full, it logs a warning before dropping. The SD card on the T-Deck is ground truth.
5. **Context cancellation is the shutdown signal.** All goroutines respect `ctx.Done()`. Shutdown is graceful: complete in-flight DB writes, close connections, flush logs.

---

## Module supervisor

The daemon runs each module under a supervisor that handles panics and restarts:

```go
func supervise(ctx context.Context, name string, fn func(context.Context) error) {
    backoff := 1 * time.Second
    for {
        err := runWithRecover(ctx, name, fn)
        if ctx.Err() != nil {
            return  // clean shutdown
        }
        log.Error().Str("module", name).Err(err).
            Dur("retry_in", backoff).Msg("module failed, restarting")
        select {
        case <-time.After(backoff):
            backoff = min(backoff*2, 5*time.Minute)
        case <-ctx.Done():
            return
        }
    }
}

func runWithRecover(ctx context.Context, name string, fn func(context.Context) error) (err error) {
    defer func() {
        if r := recover(); r != nil {
            err = fmt.Errorf("panic in module %s: %v\n%s", name, r, debug.Stack())
        }
    }()
    return fn(ctx)
}
```

Backoff starts at 1s, doubles each restart, caps at 5 minutes. Resets to 1s after a module has run successfully for >60s.

---

## Per-resource error handling

### WiFi Coconut subprocess

```
wifi_coconut exits unexpectedly
  → log ERROR with exit code
  → wait backoff
  → re-exec subprocess
  → if subprocess fails 3× in 60s: log FATAL, mark module disabled until restart
```

The Coconut subprocess is re-executed (not re-opened) because `wifi_coconut` re-enumerates USB devices on startup. If the Coconut is physically disconnected, retries fail until it's reconnected.

### Meshtastic serial

```
serial.Read returns io.EOF or error
  → log WARN "meshtastic: connection lost, reconnecting"
  → close serial port
  → wait backoff (1s → 2s → 4s → ... → 60s max)
  → scan /dev/ttyUSB* and /dev/ttyACM* for new port
  → re-open, re-handshake
  → log INFO "meshtastic: reconnected"
```

Port discovery on reconnect handles the case where the H2T is replanted in a different USB port.

### BLE scanner

```
BlueZ D-Bus connection drops
  → log WARN
  → wait 5s
  → re-establish D-Bus connection
  → re-start scan
```

### SQLite writes

```
DB write fails
  → log ERROR with query and error
  → retry up to 3× with 100ms delay
  → if all retries fail: log FATAL "database unwritable, exiting"
```

SQLite write failures are almost always disk-full or permissions — both unrecoverable. Fatal exit is correct.

### Meshtastic LoRa send (display updates to T-Deck)

```
Send fails (channel busy, no ack)
  → log DEBUG (not WARN — LoRa is best-effort)
  → drop packet
  → T-Deck display update will come on next heartbeat cycle
```

Display updates are idempotent — missing one is fine.

---

## Detection channel backpressure

The detection channel between scanner modules and the detect engine is **bounded** (capacity: 1000 records):

```go
detCh := make(chan protocol.Detection, 1000)
```

If the detect engine falls behind (e.g. slow DB write), the channel fills. Each module checks before sending:

```go
select {
case ch <- det:
default:
    log.Warn().Str("module", m.Name()).Msg("detection channel full, dropping record")
    metrics.DroppedTotal.Inc()
}
```

At typical Flock camera density, this channel will never approach capacity. The bound prevents memory growth if the detect engine stalls.

---

## Graceful shutdown sequence

```
SIGTERM / SIGINT received
  ↓
context cancelled (all modules see ctx.Done())
  ↓
modules stop reading from hardware
  ↓
detection channel drained (up to 5s timeout)
  ↓
in-flight DB writes complete
  ↓
WebSocket connections closed with 1001 Going Away
  ↓
HTTP server shutdown (up to 10s for in-flight requests)
  ↓
SQLite WAL checkpoint
  ↓
exit 0
```

systemd `TimeoutStopSec=15` gives the daemon 15 seconds to shut down gracefully before SIGKILL.

---

## Health endpoint

`GET /healthz` — no auth required, used by systemd watchdog:

```json
{
  "status": "ok",
  "modules": {
    "coconut":    {"status": "running", "uptime_s": 3612},
    "ble":        {"status": "running", "uptime_s": 3612},
    "meshtastic": {"status": "reconnecting", "attempts": 2, "next_retry_s": 4},
    "serial":     {"status": "disabled", "reason": "port not found"}
  },
  "db": {"status": "ok", "size_mb": 142},
  "detections_total": 8471,
  "uptime_s": 3612
}
```

Module statuses: `running`, `reconnecting`, `disabled`, `starting`.

systemd `WatchdogSec=30` — daemon calls `sd_notify(WATCHDOG=1)` every 10s from main loop. If it stops, systemd restarts the service.

---

## Configuration errors

Config parse errors at startup are `FATAL` — daemon exits immediately with a clear error:

```
FATAL config: [modules.meshtastic] port is required when meshtastic module is enabled
FATAL config: [auth] token must be at least 32 bytes
```

Runtime config changes (future: `SIGHUP` reload) only reload non-critical fields (log level, trusted node list). Structural changes (module enable/disable, port changes) require daemon restart.

---

## Consequences

- `supervise()` wraps every module goroutine — no module runs naked
- `runWithRecover()` defers panic handler — no panic propagates to daemon main loop
- Detection channel capacity 1000 — `metrics.DroppedTotal` counter exposed on `/healthz`
- systemd `WatchdogSec=30` + `Restart=on-failure` + `RestartSec=5` in unit file
- `GET /healthz` exempt from bearer token auth
- Shutdown timeout: 15s graceful + SIGKILL by systemd
