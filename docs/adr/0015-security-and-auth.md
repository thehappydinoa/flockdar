# ADR-0015: Security model, threat model, and web UI authentication

**Status:** Accepted  
**Date:** 2026-06-04

## Threat model

The primary adversaries are **law enforcement** and **Flock Safety / similar corporations**. Secondary: opportunistic thieves who find a deployed node.

### What they want

| Adversary | Goal |
|---|---|
| Law enforcement | Identify the operator; seize or subpoena detection data; map operator's location history |
| Flock Safety | Identify nodes scanning their infrastructure; build a counter-detection list; identify operators |
| Thief | Resell hardware; opportunistic data access |

### What we're protecting

1. **Operator identity** — who is running this
2. **Operator location history** — GPS traces reveal where you go
3. **Detection data** — the map of ALPR locations is sensitive to adversaries wanting to know what you know
4. **Node communications** — a MITM on LoRa or the API leaks detection data in transit

### Out of scope

- Nation-state adversaries with radio direction finding
- Physical device seizure with full disk forensics (use full-disk encryption at the OS level — outside this project's scope)
- Protecting the *existence* of the device from RF observers (passive scanning is inherently radio-visible)

---

## Web UI authentication

The web UI and REST API are protected by **bearer token authentication** over **TLS**.

### Token

A 32-byte cryptographically random token is generated at first run and stored in `config.toml`:

```toml
[auth]
token = "base64url-encoded-32-byte-random-token"  # generated on first run
```

All API requests must include:
```
Authorization: Bearer <token>
```

WebSocket upgrade requests must include the token as a query parameter (browsers cannot set headers on WebSocket):
```
ws://localhost:8080/ws?token=<token>
```

Requests without a valid token receive `401 Unauthorized`. The token is never logged.

### TLS

A self-signed TLS certificate is generated at first run using the Pi's hostname as the CN:

```
/var/lib/muninn/tls/cert.pem
/var/lib/muninn/tls/key.pem
```

The daemon serves HTTPS only — no HTTP fallback. Certificate fingerprint is printed to stdout on first run for manual verification. Browser warning on self-signed cert is expected and acceptable for a local LAN tool.

For LAN deployments, mDNS (`pi-pelican.local`) is the access hostname. The self-signed cert covers this hostname.

```toml
[api]
listen = "0.0.0.0:8443"
tls_cert = "/var/lib/muninn/tls/cert.pem"
tls_key  = "/var/lib/muninn/tls/key.pem"
```

### Rate limiting

Failed auth attempts are rate-limited: 5 attempts per minute per IP, then 429 with exponential backoff. This prevents token brute-force from the same subnet.

### No auth in dev mode

`--dev` flag disables TLS and auth entirely. Never use `--dev` in field deployments.

---

## Node-to-daemon authentication

Nodes authenticate to the daemon's sync endpoint using their **per-node HMAC key** (same key used to sign detection records). The sync POST verifies the HMAC on every record — a forged or replayed sync batch is rejected at the record level.

The sync endpoint also requires the bearer token if connecting over the public-facing interface. LAN-only sync (Pi Zero 2W to Pelican Hub on the same network) can use a separate internal-only listener without bearer auth:

```toml
[api]
listen_internal = "127.0.0.1:8081"  # no auth, LAN only — bind to internal interface
listen_external = "0.0.0.0:8443"    # bearer + TLS — exposed to LAN/WAN
```

---

## LoRa security

LoRa packets use the Meshtastic channel PSK (AES-256) for over-the-air encryption. Additionally, each flockdar binary payload includes its HMAC signature — a device that knows the channel key but not the HMAC key can relay packets but cannot forge them.

This provides:
- **Confidentiality** (channel PSK): eavesdroppers with a LoRa receiver cannot read the payload
- **Integrity** (HMAC): a compromised relay node cannot inject false detections

---

## Data-at-rest considerations

The daemon does not encrypt the SQLite database — that is the responsibility of the OS (LUKS full-disk encryption on the Pi, recommended but not enforced). The threat model assumes physical device seizure is addressed at the OS level.

GPS traces are stored in SQLite alongside detection records. They are never transmitted off-device except to explicitly configured sync endpoints (no telemetry, no automatic cloud sync).

---

## Token distribution to clients

The token is displayed once on daemon startup:

```
muninnd: web UI available at https://pi-pelican.local:8443
muninnd: bearer token: <token>  (also in /etc/muninn/config.toml)
```

Share the token with trusted clients via a QR code (`task auth:qr`) printed to terminal — phone camera scans it to configure the mobile web UI.

```yaml
# Taskfile.yml addition
auth:qr:
  desc: "Print web UI access QR code to terminal"
  cmds:
    - go run ./cmd/muninn auth-qr --config /etc/muninn/config.toml
```

---

## Consequences

- All API endpoints require bearer token except `/healthz` (for systemd watchdog)
- TLS cert generated on first `muninnd --config /etc/muninn/config.toml` if not present
- `task install` prints the token and cert fingerprint after deploy
- `--dev` mode: no TLS, no auth, binds to localhost only, logs a warning
- WebSocket token passed as query param — token appears in server access logs; logs are root-only readable
