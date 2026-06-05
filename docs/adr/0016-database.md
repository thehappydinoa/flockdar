# ADR-0016: SQLite with WAL mode and embedded migrations

**Status:** Accepted  
**Date:** 2026-06-04

## Decision

**SQLite** with WAL journal mode as the primary datastore for all daemon deployments. Schema migrations are embedded SQL files executed at startup via `golang-migrate/migrate`.

## Why SQLite

| Concern | SQLite | PostgreSQL |
|---|---|---|
| Deployment | Single file, zero config | Separate service, init, users |
| Pi Zero 2W fit | Yes (~8MB RAM) | No (~50MB min) |
| Concurrent writers | One at a time (sufficient for one daemon) | Many |
| Tooling | `sqlite3` CLI everywhere | `psql` |
| Backup | `cp detections.db detections.db.bak` | `pg_dump` |
| Query capability | Full SQL, CTEs, window functions | Full SQL + extensions |
| Go driver | `modernc.org/sqlite` (pure Go, no CGo) | `pgx` |

SQLite is the right choice for single-node Pi deployments. The daemon has one writer (itself) — SQLite's single-writer model is not a constraint.

`modernc.org/sqlite` is a pure Go port of SQLite — no CGo, cross-compiles cleanly for `linux/arm64` without a C toolchain. This simplifies `task build:arm64` significantly.

## WAL mode

WAL (Write-Ahead Logging) mode is set on every database open:

```sql
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;   -- safe with WAL; faster than FULL
PRAGMA cache_size=-32000;    -- 32MB page cache
PRAGMA foreign_keys=ON;
PRAGMA temp_store=MEMORY;
```

WAL allows one writer and multiple concurrent readers simultaneously — the web UI can read while the daemon writes detections.

## Schema

```sql
-- migrations/0001_initial.sql

CREATE TABLE runs (
    id          TEXT PRIMARY KEY,              -- ULID
    node_id     TEXT NOT NULL,
    name        TEXT NOT NULL,                 -- "Wandering Magpie #1"
    started_at  INTEGER NOT NULL,              -- unix seconds
    ended_at    INTEGER,
    distance_m  REAL DEFAULT 0
);

CREATE TABLE networks (
    id           TEXT PRIMARY KEY,             -- ULID
    run_id       TEXT NOT NULL REFERENCES runs(id),
    node_id      TEXT NOT NULL,
    mac          TEXT NOT NULL,
    type         TEXT NOT NULL CHECK(type IN ('wifi','ble')),
    ssid         TEXT,
    name         TEXT,                         -- BLE name
    rssi         INTEGER,
    channel      INTEGER,
    freq         INTEGER,
    capabilities TEXT,
    mfgrid       INTEGER,
    services     TEXT,                         -- space-separated BLE UUIDs
    lat          REAL,
    lon          REAL,
    ts           INTEGER NOT NULL,
    via          TEXT NOT NULL DEFAULT 'serial',
    confidence   INTEGER NOT NULL CHECK(confidence IN (1,2,3)),
    signals      TEXT NOT NULL DEFAULT '[]',   -- JSON array of {label,detail}
    vendor       TEXT,                         -- from OUI DB
    device_type  TEXT,                         -- from OUI DB
    UNIQUE(mac, run_id)
);

CREATE TABLE gaps (
    id         TEXT PRIMARY KEY,
    run_id     TEXT NOT NULL REFERENCES runs(id),
    node_id    TEXT NOT NULL,
    start_ts   INTEGER NOT NULL,
    end_ts     INTEGER NOT NULL,
    lat        REAL,
    lon        REAL,
    reason     TEXT NOT NULL DEFAULT 'unknown'
);

CREATE TABLE gps_track (
    id       TEXT PRIMARY KEY,
    run_id   TEXT NOT NULL REFERENCES runs(id),
    node_id  TEXT NOT NULL,
    lat      REAL NOT NULL,
    lon      REAL NOT NULL,
    accuracy REAL,
    fix      INTEGER,
    ts       INTEGER NOT NULL
);

CREATE TABLE nodes (
    id           TEXT PRIMARY KEY,             -- node_id string
    name         TEXT,
    last_seen    INTEGER,
    fw_version   TEXT,
    battery_pct  INTEGER,
    lat          REAL,
    lon          REAL,
    gps_fix      INTEGER DEFAULT 0
);

CREATE TABLE mac_history (
    mac          TEXT PRIMARY KEY,
    first_run_id TEXT NOT NULL,
    last_run_id  TEXT NOT NULL,
    seen_count   INTEGER NOT NULL DEFAULT 1,
    first_seen   INTEGER NOT NULL,
    last_seen    INTEGER NOT NULL
);

CREATE TABLE oui_db (
    prefix      TEXT PRIMARY KEY,              -- "b4:a5:ef" / 28-bit / 36-bit
    prefix_bits INTEGER NOT NULL,              -- 24, 28, or 36
    vendor      TEXT,
    device_type TEXT,
    icon        TEXT,
    updated_at  INTEGER NOT NULL
);

CREATE TABLE sync_log (
    id          TEXT PRIMARY KEY,
    node_id     TEXT NOT NULL,
    synced_at   INTEGER NOT NULL,
    records_in  INTEGER NOT NULL DEFAULT 0,
    records_ok  INTEGER NOT NULL DEFAULT 0,
    last_seq    INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE schema_migrations (
    version     INTEGER PRIMARY KEY,
    applied_at  INTEGER NOT NULL
);

-- indexes
CREATE INDEX idx_networks_mac      ON networks(mac);
CREATE INDEX idx_networks_run_id   ON networks(run_id);
CREATE INDEX idx_networks_ts       ON networks(ts);
CREATE INDEX idx_networks_conf     ON networks(confidence);
CREATE INDEX idx_gps_track_run     ON gps_track(run_id, ts);
CREATE INDEX idx_mac_history_last  ON mac_history(last_seen);
```

## Migrations

Migrations are embedded Go files using `golang-migrate/migrate` with the `embed` driver:

```go
//go:embed migrations/*.sql
var migrations embed.FS

func Open(path string) (*DB, error) {
    db, err := sql.Open("sqlite", path)
    // ... pragmas ...
    m, _ := migrate.NewWithSourceInstance(
        "iofs", iofs.New(migrations, "migrations"),
        "sqlite://"+path,
    )
    if err := m.Up(); err != nil && !errors.Is(err, migrate.ErrNoChange) {
        return nil, err
    }
    return &DB{db: db}, nil
}
```

Migrations run at daemon startup. They are idempotent — running twice is safe. Migration files are numbered: `0001_initial.sql`, `0002_add_oui_db.sql`, etc.

## Backup

```yaml
# Taskfile.yml
db:backup:
  desc: "Backup database from Pi to local ./backups/"
  cmds:
    - mkdir -p backups
    - ssh {{.TARGET}} "sqlite3 /var/lib/muninn/detections.db '.backup /tmp/muninnd-backup.db'"
    - scp {{.TARGET}}:/tmp/muninnd-backup.db backups/detections-$(date +%Y%m%d-%H%M%S).db
    - ssh {{.TARGET}} rm /tmp/muninnd-backup.db

db:shell:
  desc: "Open SQLite shell on Pi database (read-only)"
  cmds:
    - ssh {{.TARGET}} "sqlite3 /var/lib/muninn/detections.db"
```

## PostgreSQL upgrade path

When a future multi-operator C2 (hub-of-hubs) is built, the upgrade path is:

1. `task db:export` — dump SQLite to NDJSON
2. Stand up PostgreSQL
3. `task db:import --target postgres://...` — replay NDJSON
4. Update `config.toml`: `[store] driver = "postgres" dsn = "postgres://..."`
5. The same `golang-migrate` migrations run against PostgreSQL (SQL is compatible)

The `internal/store` package abstracts behind an interface — swapping the driver does not change calling code. This is not built now, just designed for.

## Consequences

- `modernc.org/sqlite` — pure Go, no CGo, cross-compilation works cleanly
- Schema migrations embedded in binary — no external migration tool needed on Pi
- WAL mode set on every open — safe concurrent reads from web UI
- `task db:backup` and `task db:shell` added to Taskfile
- OUI DB lives in same SQLite file — `task oui:update` upserts into `oui_db` table
- Full-disk encryption (LUKS) recommended at OS level for data-at-rest protection — not enforced by this project
