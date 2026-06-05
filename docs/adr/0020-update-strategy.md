# ADR-0020: Update and upgrade strategy

**Status:** Accepted  
**Date:** 2026-06-04

## Components to update

| Component | Mechanism | Frequency |
|---|---|---|
| Daemon binary | `task install` or self-update | On release |
| DB schema | Embedded migrations, auto on startup | On release |
| Signatures | `task sigs:pull` / LoRa OTA | Anytime |
| OUI database | `task oui:update` | Quarterly |
| ESP32 firmware | `task firmware:flash` (USB) | On release |
| Map tiles | `task tiles:download` | Annually |
| TLS cert | Auto-renew at expiry | Yearly |

---

## Daemon binary update

### `task install` (standard)

Builds `linux/arm64`, SCPs to Pi, restarts systemd service:

```bash
task install TARGET=pi-pelican.local
# → cross-compiles, scp, systemctl restart
# → systemd stops old binary gracefully (15s), starts new
# → migrations run on startup if schema changed
```

### Self-update command

The daemon can update itself from GitHub releases:

```bash
flockdard update          # downloads latest release binary, replaces self, signals systemd
flockdard update --check  # prints available version, does not update
```

Implementation: fetches `https://github.com/<org>/<repo>/releases/latest`, downloads the `linux-arm64` asset, verifies SHA256 checksum from the release manifest, atomically replaces `/usr/local/bin/flockdard` via temp file rename, sends `SIGHUP` to systemd to trigger a graceful restart.

```yaml
# Taskfile.yml
update:remote:
  desc: "Self-update daemon on Pi (TARGET=hostname)"
  cmds:
    - ssh {{.TARGET}} "/usr/local/bin/flockdard update"
```

---

## Schema migrations

Migrations are embedded SQL files in the binary (ADR-0016). They run automatically at startup before any module starts:

```
startup sequence:
  1. open SQLite
  2. run pending migrations (if any)
  3. log INFO "migrations: applied N new migrations" or "up to date"
  4. start modules
```

Migration files are append-only — never edit an existing migration. Add a new numbered file for changes.

**Rollback**: SQLite does not support transactional DDL across all statements, so migrations are not automatically reversible. The pre-update backup (`task db:backup`) is the rollback path:

```bash
# Before updating
task db:backup TARGET=pi-pelican.local

# If update goes wrong, restore
scp backups/detections-20260604-120000.db pi-pelican.local:/var/lib/flockdard/detections.db
ssh pi-pelican.local systemctl restart flockdard
```

---

## Signature updates

### Pull from feed (network)

```bash
task sigs:pull   # fetches signatures.toml from configured URL, runs task gen:oui
```

```toml
[signatures]
feed_url = "https://raw.githubusercontent.com/<org>/<repo>/main/signatures.toml"
auto_pull = true      # pull on daemon startup if internet available
pull_interval = "24h" # background re-check interval
```

### LoRa OTA to T-Deck

When the daemon gets updated signatures, it notifies connected T-Decks on next LoRa heartbeat:

```
Pi daemon: signature hash changed
  → sends FlockdarSigUpdate packet over LoRa: {new_hash, download_size}
  → T-Deck: "new signatures available"
  → on next LoRa window: daemon sends signatures.bin in chunks (binary-encoded TOML subset)
  → T-Deck: writes to SD /flockdar/signatures.bin, reloads OUI table from SD
  → no reflash required
```

This is Phase 7 work — the chunked LoRa transfer protocol is complex. For now, signatures are updated by flashing new firmware via USB.

---

## ESP32 firmware update

Current: USB flash only via PlatformIO.

```bash
# Build and flash T-Deck
task firmware:flash

# Flash to remote Pi-attached ESP32
task firmware:flash:remote TARGET=pi-pelican.local PORT=/dev/ttyUSB1
```

```yaml
firmware:flash:remote:
  desc: "Build firmware and flash via Pi serial passthrough (TARGET, PORT)"
  cmds:
    - task: firmware:build
    - scp esp32/.pio/build/t-deck/firmware.bin {{.TARGET}}:/tmp/fw.bin
    - ssh {{.TARGET}} "esptool.py --port {{.PORT}} write_flash 0x0 /tmp/fw.bin"
```

Future: OTA via WiFi using ESP32 OTA partition. The T-Deck could download firmware from the daemon's `/api/v1/firmware/latest` endpoint when on a trusted WiFi network. Not in scope for v1.

---

## TLS certificate renewal

Self-signed certs are generated with a 2-year expiry. The daemon checks cert expiry at startup and logs `WARN` if expiry is within 30 days:

```
WARN tls: certificate expires in 28 days — run 'flockdard renew-cert' to regenerate
```

```bash
flockdard renew-cert   # generates new self-signed cert, restarts TLS listener
```

Or via Taskfile:

```bash
task cert:renew TARGET=pi-pelican.local
```

---

## OUI database update

```bash
task oui:update   # fetches IEEE MA-L/M/S/CID CSVs, rebuilds oui_db table
```

Run quarterly — OUI assignments change slowly. Included in the install task for first-run setup.

---

## Map tile update

```bash
task tiles:download REGION=us   # downloads fresh Protomaps extract
```

Run annually — OSM data is updated but tile extracts are stable. New extract replaces the old file atomically (download to temp path, rename).

---

## Version pinning

The daemon binary includes its version, Git commit SHA, and build timestamp in `GET /api/v1/stats`:

```json
{
  "version": "0.4.1",
  "commit":  "a1b2c3d",
  "built":   "2026-06-04T12:00:00Z",
  "go":      "go1.22.3"
}
```

Firmware version is reported in `info` protocol records and visible in the node dashboard.

## Consequences

- `flockdard update` sub-command added with SHA256 verification
- Embedded migrations run before modules start — zero manual migration steps
- `task db:backup` should be run before any daemon update (documented in upgrade runbook)
- `task sigs:pull` added for signature feed integration
- `task firmware:flash:remote` enables flashing from dev machine via Pi serial passthrough
- TLS cert expiry warning at 30 days, `flockdard renew-cert` sub-command
