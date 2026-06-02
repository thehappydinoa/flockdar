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

    # list / dump microSD wardrive logs over serial (T-Deck, FD_ENABLE_SD)
    uv run flockdar-ingest COM3 --sd-list
    uv run flockdar-ingest COM3 --sd-dump last -o last-run.ndjson
    uv run flockdar-ingest COM3 --sd-dump last --gps-summary

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
import time
from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from datetime import datetime
from hashlib import sha256
from pathlib import Path
from typing import Any

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

    lat = float(obj.get("lat", 0.0) or 0.0) if "lat" in obj else pos.get("lat", 0.0)
    lon = float(obj.get("lon", 0.0) or 0.0) if "lon" in obj else pos.get("lon", 0.0)

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


def _parse_json_line(line: str) -> dict[str, Any] | None:
    """Parse one NDJSON line; tolerate a leading '{' duplicated by USB framing."""
    for _ in range(3):
        try:
            obj = json.loads(line)
        except (json.JSONDecodeError, ValueError):
            if line.startswith("{"):
                line = line[1:]
                continue
            return None
        return obj if isinstance(obj, dict) else None
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
        nmea = int(obj.get("nmea", 0) or 0)
        if fix is True or fix == "true":
            state = "FIX"
        elif nmea >= 10:
            state = "acquiring"
        elif not (obj.get("module") is True or obj.get("module") == "true"):
            state = "no module"
        else:
            state = "no NMEA"
        chip = obj.get("chip", "")
        chip_s = f" chip={chip}" if chip else ""
        return (
            f"[{ts}ms] fw={fw}  gps {state}{chip_s}  "
            f"nmea={obj.get('nmea', 0)} sats={obj.get('sats', 0)}"
        )
    if etype == "gps":
        return f"[{ts}ms] fw={fw}  gps FIX  acc={obj.get('accuracy', '?')}m"
    return None


def monitor_stream(lines: Iterable[str]) -> None:
    """Print firmware info/gps status lines; ignore wifi/ble detections."""
    for line in lines:
        line = line.strip()
        if not line:
            continue
        obj = _parse_json_line(line)
        if obj is None:
            print(f"?? {line}", file=sys.stderr)
            continue
        formatted = _format_monitor_line(obj)
        if formatted:
            print(formatted, flush=True)


# ---------------------------------------------------------------------------
# Stream -> Records / Hits
# ---------------------------------------------------------------------------


def iter_records(
    lines: Iterable[str], key: bytes, verify: bool = True
) -> Iterator[tuple[Record, str, str]]:
    """Parse a line stream, verify signatures, track GPS, yield (record, method, detail).

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
            rec, method = mapped
            detail = str(obj.get("detail", "") or method)
            yield rec, method, detail


def iter_hits(lines: Iterable[str], key: bytes, verify: bool = True) -> Iterator[detect.Hit]:
    """Yield a Hit per detection line (no dedup — see merge_hits)."""
    for rec, method, detail in iter_records(lines, key, verify=verify):
        hit = detect.analyze(**rec)
        if hit:
            if method:
                hit.add_signal("ESP32_LIVE", detail)
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


def _serial_open(port: str, baud: int):
    """Open a serial port without toggling DTR/RTS (avoids ESP32 USB-CDC reset)."""
    try:
        import serial  # type: ignore
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError(
            "pyserial is required for serial access; install with `uv sync`"
        ) from exc
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 1
    # ESP32-S3 native USB resets on DTR/RTS edges (same mechanism as `pio upload`).
    ser.dtr = False
    ser.rts = False
    try:
        ser.open()
    except serial.SerialException as exc:
        raise RuntimeError(f"cannot open serial port {port}: {exc}") from exc
    ser.dtr = False
    ser.rts = False
    return ser


def _serial_close(ser: Any) -> None:
    """Close without pulsing DTR/RTS (Ctrl-C otherwise reboots the device)."""
    if ser is None:
        return
    try:
        if getattr(ser, "is_open", False):
            ser.dtr = False
            ser.rts = False
            ser.close()
    except Exception:
        pass


def serial_lines(
    port: str, baud: int = DEFAULT_BAUD, should_stop: Callable[[], bool] | None = None
) -> Iterator[str]:
    """Yield decoded lines from a serial port until should_stop() is true.

    pyserial is imported lazily so file/log ingestion works without it.
    """
    ser = _serial_open(port, baud)
    try:
        while True:
            if should_stop and should_stop():
                return
            raw = ser.readline()
            if not raw:
                continue  # timeout — loop back to re-check should_stop
            yield raw.decode("utf-8", "replace")
    finally:
        _serial_close(ser)


def _info_msg(obj: dict[str, Any]) -> str:
    return str(obj.get("msg", "") or "")


def _line_info_msg(line: str) -> str | None:
    """Extract firmware info msg from a serial line (JSON or substring fallback)."""
    obj = _parse_json_line(line)
    if obj is not None and obj.get("type") == "info":
        return _info_msg(obj)
    if '"type":"info"' in line or '"type": "info"' in line:
        m = re.search(r'"msg"\s*:\s*"([^"\\]*(?:\\.[^"\\]*)*)"', line)
        if m:
            return m.group(1).encode().decode("unicode_escape")
    return None


def _send_sd_abort(ser: Any) -> None:
    try:
        ser.write(b"sd abort\n")
        ser.flush()
        time.sleep(0.15)
    except Exception:
        pass


@dataclass
class SdDumpResult:
    lines: list[str]
    complete: bool = False
    interrupted: bool = False
    timed_out: bool = False


def normalize_sd_dump_target(target: str | None) -> str | None:
    """Accept ``46``, ``flock-46``, or ``/flock-0046.ndjson`` for the firmware."""
    if target is None or target == "":
        return None
    if target == "last":
        return "last"
    m = re.match(r"^(?:/)?flock-(\d+)(?:\.ndjson)?$", target, re.IGNORECASE)
    if m:
        return f"/flock-{int(m.group(1)):04d}.ndjson"
    if re.fullmatch(r"\d+", target):
        return f"/flock-{int(target):04d}.ndjson"
    if target.startswith("/"):
        return target
    return f"/{target}"


def serial_sd_list(port: str, baud: int = DEFAULT_BAUD) -> list[tuple[str, int]]:
    """Send ``sd list``; return [(path, bytes), ...] from the card."""
    ser = _serial_open(port, baud)
    files: list[tuple[str, int]] = []
    interrupted = False
    try:
        time.sleep(0.3)
        ser.reset_input_buffer()
        ser.write(b"sd list\n")
        ser.flush()
        deadline = time.time() + 30.0
        while time.time() < deadline:
            try:
                raw = ser.readline()
            except KeyboardInterrupt:
                interrupted = True
                break
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            msg = _line_info_msg(line)
            if msg and msg.startswith("sd list end"):
                break
            if msg and msg.startswith("sd file "):
                parts = msg.split()
                if len(parts) >= 4:
                    files.append((parts[2], int(parts[3])))
    finally:
        _serial_close(ser)
    if interrupted:
        print("\nList interrupted.", file=sys.stderr)
    return files


def _parse_sd_dump_ack(msg: str) -> int | None:
    """Return expected byte size from ``sd dump ack /path N``."""
    parts = msg.split()
    if len(parts) >= 4 and parts[-1].isdigit():
        return int(parts[-1])
    return None


def serial_sd_dump(
    port: str,
    target: str | None = None,
    baud: int = DEFAULT_BAUD,
    timeout: float = 600.0,
) -> SdDumpResult:
    """Send ``sd dump`` and collect raw NDJSON lines from the card."""
    ser = _serial_open(port, baud)
    rows: list[str] = []
    capturing = False
    saw_ack = False
    interrupted = False
    expected_bytes: int | None = None
    received_bytes = 0
    last_progress = time.time()
    last_pct = -1
    try:
        time.sleep(0.3)
        ser.reset_input_buffer()
        cmd = "sd dump" if not target else f"sd dump {target}"
        ser.write((cmd + "\n").encode("utf-8"))
        ser.flush()
        print("  waiting for device...", file=sys.stderr, flush=True)
        start = time.time()
        deadline = start + timeout
        nudge_at = start + 20.0
        while time.time() < deadline:
            try:
                raw = ser.readline()
            except KeyboardInterrupt:
                interrupted = True
                _send_sd_abort(ser)
                break
            if not raw:
                if capturing and time.time() - last_progress > 30.0:
                    print("  (still receiving...)", file=sys.stderr, flush=True)
                    last_progress = time.time()
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            msg = _line_info_msg(line)
            if not capturing:
                if msg and msg.startswith("sd dump ack"):
                    saw_ack = True
                    expected_bytes = _parse_sd_dump_ack(msg)
                    print(f"  {msg}", file=sys.stderr, flush=True)
                elif msg and msg.startswith("sd dump fail"):
                    raise RuntimeError(msg)
                elif (msg and msg.startswith("sd dump begin")) or ("sd dump begin" in line):
                    capturing = True
                    if msg:
                        print(f"  {msg}", file=sys.stderr, flush=True)
                    else:
                        print("  transfer started", file=sys.stderr, flush=True)
                elif not saw_ack and time.time() >= nudge_at:
                    print(
                        "  still waiting (close other serial port users)",
                        file=sys.stderr,
                        flush=True,
                    )
                    nudge_at = time.time() + 20.0
                continue
            if (msg and msg.startswith("sd dump end")) or "sd dump end" in line:
                if msg:
                    print(f"  {msg}", file=sys.stderr, flush=True)
                return SdDumpResult(rows, complete=True)
            if (msg and msg.startswith("sd dump aborted")) or "sd dump aborted" in line:
                return SdDumpResult(rows, interrupted=True)
            rows.append(line)
            received_bytes += len(line) + 1
            now = time.time()
            if expected_bytes and expected_bytes > 0:
                pct = min(100, received_bytes * 100 // expected_bytes)
                if pct >= last_pct + 10 or pct == 100:
                    print(
                        f"  {pct}% ({received_bytes}/{expected_bytes} bytes)",
                        file=sys.stderr,
                        flush=True,
                    )
                    last_pct = pct
            elif len(rows) % 500 == 0 or now - last_progress >= 5.0:
                print(f"  {len(rows)} lines...", file=sys.stderr, flush=True)
                last_progress = now
        if interrupted:
            return SdDumpResult(rows, interrupted=True)
        return SdDumpResult(rows, timed_out=True)
    finally:
        _serial_close(ser)


def gps_log_summary(lines: Iterable[str]) -> str:
    """Summarise gps_status / gps lines from an NDJSON log."""
    statuses: list[dict[str, Any]] = []
    fixes: list[dict[str, Any]] = []
    fw = "?"
    for line in lines:
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except (json.JSONDecodeError, ValueError):
            continue
        if obj.get("type") == "gps_status":
            statuses.append(obj)
            fw = str(obj.get("fw", fw))
        elif obj.get("type") == "gps":
            fixes.append(obj)
            fw = str(obj.get("fw", fw))
    if not statuses and not fixes:
        return (
            "No GPS lines in this log. If the run predates gps_status logging, "
            "check wifi/ble lines for inline lat/lon."
        )
    out: list[str] = [f"Firmware: {fw}", f"gps_status samples: {len(statuses)}"]
    if statuses:
        last = statuses[-1]
        chip = last.get("chip", "?")
        out.append(
            "Last gps_status: "
            f"chip={chip} fix={last.get('fix')} sats={last.get('sats')} "
            f"nmea={last.get('nmea')} module={last.get('module')}"
        )
        max_sats = max(int(s.get("sats", 0) or 0) for s in statuses)
        out.append(f"Peak satellites seen: {max_sats}")
        if max_sats == 0 and int(last.get("nmea", 0) or 0) > 100:
            out.append(
                "NMEA flowing but no fix yet — check antenna/sky view, or wrong "
                "GPS chip init (see esp32/BOARDS.md)."
            )
    out.append(f"gps fix lines: {len(fixes)}")
    if fixes:
        f0 = fixes[0]
        out.append(
            f"First fix: {f0.get('lat')},{f0.get('lon')} "
            f"acc={f0.get('accuracy')}m @ {f0.get('ts_ms')}ms"
        )
    return "\n".join(out)


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
        help="print firmware info/gps status only (verify fw version / GPS)",
    )
    ap.add_argument(
        "--sd-list",
        action="store_true",
        help="list flock-*.ndjson files on the device SD card (serial only)",
    )
    ap.add_argument(
        "--sd-dump",
        nargs="?",
        const="",
        metavar="PATH",
        help="dump SD log: 46, flock-46, /flock-0046.ndjson, last, or omit for current",
    )
    ap.add_argument(
        "--gps-summary",
        action="store_true",
        help="with --sd-dump, print GPS stats from the log",
    )
    args = ap.parse_args(argv)

    key = resolve_key(args.key)
    verify = not args.no_verify

    if args.sd_list or args.sd_dump is not None:
        if not is_serial_source(args.source):
            print("--sd-list / --sd-dump require a serial port", file=sys.stderr)
            return 2
        if args.sd_list:
            for path, size in serial_sd_list(args.source, args.baud):
                print(f"{path}  {size} bytes")
            return 0
        target = normalize_sd_dump_target(args.sd_dump or None)
        print(f"Dumping SD log from {args.source}...", file=sys.stderr)
        if target:
            print(f"  file: {target}", file=sys.stderr)
        try:
            result = serial_sd_dump(args.source, target=target, baud=args.baud)
        except RuntimeError as exc:
            print(f"Error: {exc}", file=sys.stderr)
            return 1
        except KeyboardInterrupt:
            print("\nInterrupted.", file=sys.stderr)
            return 130
        if result.interrupted:
            print(
                f"Dump interrupted — kept {len(result.lines)} line(s).",
                file=sys.stderr,
            )
        elif result.timed_out:
            print(
                f"Dump timed out — got {len(result.lines)} line(s) so far.",
                file=sys.stderr,
            )
        elif result.complete:
            print(f"Done — {len(result.lines)} line(s).", file=sys.stderr)
        rows = result.lines
        if args.output:
            Path(args.output).write_text("\n".join(rows) + ("\n" if rows else ""), encoding="utf-8")
            print(f"Wrote {len(rows)} line(s) -> {args.output}", file=sys.stderr)
        else:
            for row in rows:
                print(row)
        if args.gps_summary and rows:
            print("\n--- GPS summary ---", file=sys.stderr)
            print(gps_log_summary(rows), file=sys.stderr)
        return 130 if result.interrupted else 0

    if is_serial_source(args.source):
        lines: Iterable[str] = serial_lines(args.source, args.baud)
        label = f"Reading {args.source} @ {args.baud} baud"
    else:
        lines = log_lines(args.source)
        label = f"Reading {args.source}"

    if args.monitor:
        print(f"{label} — Ctrl-C to stop (port stays open until you exit).", file=sys.stderr)
        try:
            monitor_stream(lines)
        except KeyboardInterrupt:
            print("\nStopped (serial port closed).", file=sys.stderr)
        return 130

    if is_serial_source(args.source):
        print(f"{label} — Ctrl-C to stop.", file=sys.stderr)

    records: list[Record] = []
    interrupted = False
    try:
        for rec, method, detail in iter_records(lines, key, verify=verify):
            records.append(rec)
            hit = detect.analyze(**rec)
            if hit:
                if method:
                    hit.add_signal("ESP32_LIVE", detail)
                print(_summary(hit, method))
    except KeyboardInterrupt:
        interrupted = True
        print(f"\nStopped after {len(records)} detection(s).", file=sys.stderr)

    if args.output:
        n = write_sqlite(records, args.output)
        print(f"Wrote {n} record(s) -> {args.output}", file=sys.stderr)
    return 130 if interrupted else 0


if __name__ == "__main__":
    raise SystemExit(main())
