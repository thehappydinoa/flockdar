"""
Ingest the flockdar-esp32 JSON stream — from a live serial port or a saved
NDJSON log (e.g. the firmware's SD-card file) — into `detect.Hit` objects.

No UI dependencies; importable from the TUI or run standalone:

    # live serial -> human-readable summary
    uv run flockdar-ingest /dev/ttyUSB0
    uv run flockdar-ingest COM3                         # Windows

    # live serial -> WiGLE-format SQLite the TUI can open (Ctrl-C to stop)
    uv run flockdar-ingest /dev/ttyUSB0 out.sqlite
    uv run flockdar out.sqlite

    # replay an SD-card log
    uv run flockdar-ingest flock-0001.ndjson out.sqlite

    # debug GPS / firmware version over serial (no detections)
    uv run flockdar-ingest COM3 --monitor

(`flockdar-ingest` is the console script; `python -m flockdar.serial_import`
works too.)

The firmware signs `wifi`/`ble` lines with HMAC-SHA256 (see esp32/README.md);
this module verifies them with the shared key before trusting a detection.
`gps` lines update the running position that subsequent detections inherit.
Wifi/ble lines may also carry inline `lat`/`lon`/`accuracy` when stamped at
capture time (preferred over the running GPS position).
"""

from __future__ import annotations

import argparse
import gzip
import hmac
import json
import os
import re
import sqlite3
import sys
from datetime import datetime
from hashlib import sha256
from pathlib import Path
from typing import Any, Callable, Iterable, Iterator

from . import detect

DEFAULT_BAUD = 115200
DEFAULT_HMAC_KEY = "flockdar-dev-key"  # matches the firmware build-flag default
ENV_HMAC_KEY = "FLOCKDAR_HMAC_KEY"

# Matches the trailing signature the firmware appends just before the closing
# brace:  ...,"ts_ms":123,"sig":"a1b2c3d4"}
_SIG_RE = re.compile(r',"sig":"([0-9a-f]{8})"\}$')

Record = dict[str, Any]


# ---------------------------------------------------------------------------
# Signing / verification
# ---------------------------------------------------------------------------

def resolve_key(key: str | None) -> bytes:
    """Resolve the HMAC key from arg, env, or the firmware default."""
    return (key or os.environ.get(ENV_HMAC_KEY) or DEFAULT_HMAC_KEY).encode()


def verify_line(line: str, key: bytes) -> bool:
    """True if line carries a valid HMAC signature for the shared key.

    Reconstructs the signed bytes by stripping the trailing ,"sig":"<hex>"
    back to a closing brace — the inverse of how the firmware inserts it.
    """
    line = line.strip()
    m = _SIG_RE.search(line)
    if not m:
        return False
    payload = _SIG_RE.sub("}", line)
    calc = hmac.new(key, payload.encode(), sha256).hexdigest()[:8]
    return hmac.compare_digest(calc, m.group(1))


# ---------------------------------------------------------------------------
# JSON line -> detect Record
# ---------------------------------------------------------------------------

def _channel_to_mhz(channel: int) -> int:
    """2.4 GHz channel number -> centre frequency in MHz (ch 1..13)."""
    if 1 <= channel <= 13:
        return 2407 + 5 * channel
    return 0


def line_to_record(obj: dict[str, Any], pos: dict[str, float]) -> tuple[Record, str] | None:
    """Map a parsed wifi/ble event to a detect Record + its detection method.

    Returns None for non-detection events. `pos` carries the latest GPS fix.
    """
    etype = obj.get("type")
    method = str(obj.get("method", ""))
    mac = str(obj.get("mac", "")).strip()
    if not mac:
        return None

    if "lat" in obj:
        lat = float(obj.get("lat", 0.0) or 0.0)
    else:
        lat = pos.get("lat", 0.0)
    if "lon" in obj:
        lon = float(obj.get("lon", 0.0) or 0.0)
    else:
        lon = pos.get("lon", 0.0)

    base: Record = {
        "mac": mac,
        "lat": lat,
        "lon": lon,
        "rssi": int(obj.get("rssi", 0) or 0),
        "first_seen": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "services": "",
    }

    if etype == "wifi":
        return (
            {
                **base,
                "ssid": "",  # promiscuous capture doesn't record SSID
                "type": "WIFI",
                "frequency": _channel_to_mhz(int(obj.get("channel", 0) or 0)),
                "capabilities": "",
                "mfgrid": 0,
            },
            method,
        )

    if etype == "ble":
        return (
            {
                **base,
                "ssid": str(obj.get("name", "") or ""),
                "type": "BLE",
                "frequency": 0,
                "capabilities": "",
                "mfgrid": int(obj.get("mfgrid", 0) or 0),
            },
            method,
        )

    return None


def _format_monitor_line(obj: dict[str, Any]) -> str | None:
    """Human-readable line for firmware status events (info/gps/gps_status)."""
    etype = obj.get("type")
    fw = obj.get("fw", "?")
    ts = obj.get("ts_ms", "?")
    if etype == "info":
        return f"[{ts}ms] fw={fw}  {obj.get('msg', '')}"
    if etype == "gps_status":
        fix = obj.get("fix")
        state = "FIX" if fix is True or fix == "true" else "acquiring"
        if not (obj.get("module") is True or obj.get("module") == "true"):
            state = "no module"
        elif int(obj.get("nmea", 0) or 0) < 10:
            state = "no NMEA"
        return (
            f"[{ts}ms] fw={fw}  gps {state}  "
            f"nmea={obj.get('nmea', 0)} sats={obj.get('sats', 0)}"
        )
    if etype == "gps":
        return (
            f"[{ts}ms] fw={fw}  gps FIX  "
            f"{obj.get('lat')},{obj.get('lon')} "
            f"acc={obj.get('accuracy', '?')}m"
        )
    return None


def monitor_stream(lines: Iterable[str]) -> None:
    """Print firmware info/gps status lines; ignore wifi/ble detections."""
    for line in lines:
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except (json.JSONDecodeError, ValueError):
            print(f"?? {line}", file=sys.stderr)
            continue
        formatted = _format_monitor_line(obj)
        if formatted:
            print(formatted)


# ---------------------------------------------------------------------------
# Stream -> Records / Hits
# ---------------------------------------------------------------------------

def iter_records(
    lines: Iterable[str], key: bytes, verify: bool = True
) -> Iterator[tuple[Record, str]]:
    """Parse a line stream, verify signatures, track GPS, yield (record, method).

    `gps` events update the running position; `info` and unsigned/forged
    detection lines are skipped.
    """
    pos: dict[str, float] = {"lat": 0.0, "lon": 0.0}
    for line in lines:
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except (json.JSONDecodeError, ValueError):
            continue
        etype = obj.get("type")
        if etype == "gps":
            pos["lat"] = float(obj.get("lat", pos["lat"]))
            pos["lon"] = float(obj.get("lon", pos["lon"]))
            continue
        if etype not in ("wifi", "ble"):
            continue
        if verify and not verify_line(line, key):
            continue
        mapped = line_to_record(obj, pos)
        if mapped:
            yield mapped


def iter_hits(
    lines: Iterable[str], key: bytes, verify: bool = True
) -> Iterator[detect.Hit]:
    """Yield a Hit per detection line (no dedup — see merge_hits)."""
    for rec, method in iter_records(lines, key, verify=verify):
        hit = detect.analyze(**rec)
        if hit:
            if method:
                hit.add_signal("ESP32_LIVE", method)
            yield hit


def merge_hit(seen: dict[str, detect.Hit], hit: detect.Hit) -> bool:
    """Merge hit into the seen-by-MAC map. Returns True if it's a new MAC."""
    key = hit.mac.lower()
    existing = seen.get(key)
    if existing is None:
        seen[key] = hit
        return True
    for s in hit.signals:
        existing.add_signal(*s)
    if hit.rssi > existing.rssi:
        existing.rssi = hit.rssi
    if (hit.lat or hit.lon) and not (existing.lat or existing.lon):
        existing.lat, existing.lon = hit.lat, hit.lon
    return False


def load_log(path: Path, key: bytes, verify: bool = True) -> tuple[list[detect.Hit], int]:
    """Read a saved NDJSON log, returning (deduped hits, detection lines seen)."""
    seen: dict[str, detect.Hit] = {}
    total = 0
    for hit in iter_hits(log_lines(path), key, verify=verify):
        total += 1
        merge_hit(seen, hit)
    hits = sorted(seen.values(), key=lambda h: (-h.confidence, h.mac))
    return hits, total


# ---------------------------------------------------------------------------
# Line sources
# ---------------------------------------------------------------------------

def is_serial_source(source: str) -> bool:
    """Heuristic: a regular file is a log; anything else is a serial device."""
    if re.match(r"^COM\d+$", source, re.IGNORECASE):
        return True
    return not Path(source).is_file()


def log_lines(path: Path | str) -> Iterator[str]:
    p = str(path)
    opener = gzip.open if p.endswith(".gz") else open
    with opener(p, "rt", encoding="utf-8", errors="replace") as f:
        yield from f


def serial_lines(
    port: str, baud: int = DEFAULT_BAUD, should_stop: Callable[[], bool] | None = None
) -> Iterator[str]:
    """Yield decoded lines from a serial port until should_stop() is true.

    pyserial is imported lazily so file/log ingestion works without it.
    """
    try:
        import serial  # type: ignore
    except ImportError as exc:  # pragma: no cover - depends on install
        raise RuntimeError(
            "pyserial is required for --serial; install with `uv sync`"
        ) from exc

    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as exc:  # bad port, permission denied, busy
        raise RuntimeError(f"cannot open serial port {port}: {exc}") from exc
    try:
        while True:
            if should_stop and should_stop():
                return
            raw = ser.readline()
            if not raw:
                continue  # timeout — loop back to re-check should_stop
            yield raw.decode("utf-8", "replace")
    finally:
        ser.close()


# ---------------------------------------------------------------------------
# SQLite writer (WiGLE-compatible, so the TUI can open the result)
# ---------------------------------------------------------------------------

_TYPE_CHAR = {"WIFI": "W", "BLE": "E"}


def write_sqlite(records: Iterable[Record], path: str) -> int:
    """Write records to a WiGLE-format `network` table. Returns rows written."""
    conn = sqlite3.connect(path)
    try:
        conn.execute(
            "CREATE TABLE IF NOT EXISTS network ("
            "bssid TEXT PRIMARY KEY, ssid TEXT, type TEXT, bestlevel INTEGER, "
            "bestlat REAL, bestlon REAL, lasttime TEXT, service TEXT, "
            "frequency INTEGER, capabilities TEXT, mfgrid INTEGER)"
        )
        rows = 0
        for rec in records:
            conn.execute(
                "INSERT INTO network "
                "(bssid, ssid, type, bestlevel, bestlat, bestlon, lasttime, "
                " service, frequency, capabilities, mfgrid) "
                "VALUES (?,?,?,?,?,?,?,?,?,?,?) "
                "ON CONFLICT(bssid) DO UPDATE SET "
                "  bestlevel=MAX(network.bestlevel, excluded.bestlevel), "
                "  bestlat=COALESCE(NULLIF(network.bestlat,0), excluded.bestlat), "
                "  bestlon=COALESCE(NULLIF(network.bestlon,0), excluded.bestlon), "
                "  lasttime=excluded.lasttime, "
                "  ssid=COALESCE(NULLIF(excluded.ssid,''), network.ssid), "
                "  mfgrid=MAX(network.mfgrid, excluded.mfgrid)",
                (
                    rec["mac"],
                    rec.get("ssid", ""),
                    _TYPE_CHAR.get(rec.get("type", ""), rec.get("type", "")),
                    rec.get("rssi", 0),
                    rec.get("lat", 0.0),
                    rec.get("lon", 0.0),
                    rec.get("first_seen", ""),
                    rec.get("services", ""),
                    rec.get("frequency", 0),
                    rec.get("capabilities", ""),
                    rec.get("mfgrid", 0),
                ),
            )
            rows += 1
        conn.commit()
        return rows
    finally:
        conn.close()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _summary(hit: detect.Hit, method: str) -> str:
    return (
        f"[{hit.confidence_label:>6}] {hit.device_type:<4} {hit.mac}  "
        f"rssi={hit.rssi:<4} {method:<13} {hit.ssid or ''}".rstrip()
    )


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Ingest the flockdar-esp32 JSON stream (serial or NDJSON log)."
    )
    ap.add_argument("source", help="serial port (/dev/ttyUSB0, COM3) or NDJSON log file")
    ap.add_argument("output", nargs="?", help="optional output .sqlite (TUI-openable)")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--key", help=f"HMAC key (or ${ENV_HMAC_KEY}; default firmware key)")
    ap.add_argument("--no-verify", action="store_true", help="skip HMAC verification")
    ap.add_argument(
        "--monitor",
        action="store_true",
        help="print firmware info/gps status only (debug GPS or verify fw version)",
    )
    args = ap.parse_args(argv)

    key = resolve_key(args.key)
    verify = not args.no_verify

    if is_serial_source(args.source):
        lines: Iterable[str] = serial_lines(args.source, args.baud)
        label = f"Reading {args.source} @ {args.baud} baud"
    else:
        lines = log_lines(args.source)
        label = f"Reading {args.source}"

    if args.monitor:
        print(f"{label} — Ctrl-C to stop.", file=sys.stderr)
        try:
            monitor_stream(lines)
        except KeyboardInterrupt:
            print("\nStopped.", file=sys.stderr)
        return 0

    if is_serial_source(args.source):
        print(f"{label} — Ctrl-C to stop.", file=sys.stderr)

    records: list[Record] = []
    try:
        for rec, method in iter_records(lines, key, verify=verify):
            records.append(rec)
            hit = detect.analyze(**rec)
            if hit:
                if method:
                    hit.add_signal("ESP32_LIVE", method)
                print(_summary(hit, method))
    except KeyboardInterrupt:
        print("\nStopped.", file=sys.stderr)

    if args.output:
        n = write_sqlite(records, args.output)
        print(f"Wrote {n} record(s) -> {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
