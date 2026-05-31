"""
Raspberry Pi (Linux) native scanning — use the host's own Wi-Fi and Bluetooth
radios to detect Flock devices, no ESP32 required. The Pi has the headroom to
run both radios and ingest the result straight into the same pipeline.

Two scanners feed the usual `detect.analyze()` pipeline:

  * Wi-Fi — periodic `iw dev <iface> scan` (active scan) parsed into records.
    Catches Flock camera SSIDs (`Flock-XXXXXX`), `flocknet`, and chip-vendor
    OUIs. Needs CAP_NET_ADMIN: run with sudo, or `setcap` the iw binary.
  * BLE — a bleak passive advertisement scan (optional `pi` extra). Catches
    Raven GATT service UUIDs, Penguin manufacturer id 2504, and `FS Ext
    Battery` names — the strongest native signals.

Both run on background threads and push records onto a queue; the generator
drains it, stamps an optional GPS fix (e.g. from a Meshtastic node), runs
`analyze()`, and yields Hits carrying a `PI_NATIVE` provenance signal. Either
scanner can fail to start (missing tool/extra, permissions) without stopping
the other — failures surface through `on_status`.

The pure mappers (`parse_iw_scan`, `ble_record`) are unit-tested; the radio I/O
is thin and lazily imported.
"""

from __future__ import annotations

import argparse
import queue
import re
import sys
import threading
import time
from datetime import datetime
from typing import Any, Callable, Iterator, Optional

from . import detect
from . import serial_import  # write_sqlite + _summary reuse

Record = dict[str, Any]
Position = tuple[float, float]

_BSS_RE = re.compile(r"BSS ([0-9a-fA-F:]{17})")


def _now() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


# ---------------------------------------------------------------------------
# Pure record mappers
# ---------------------------------------------------------------------------

def _wifi_record(mac: str, ssid: str, rssi: int, freq: int) -> Record:
    return {
        "mac": mac.lower(),
        "ssid": ssid,
        "type": "WIFI",
        "rssi": rssi,
        "lat": 0.0,
        "lon": 0.0,
        "first_seen": _now(),
        "services": "",
        "frequency": freq,
        "capabilities": "",
        "mfgrid": 0,
    }


def parse_iw_scan(text: str) -> list[Record]:
    """Parse `iw dev <iface> scan` output into WiFi records (position 0/0)."""
    records: list[Record] = []
    cur: Optional[dict[str, Any]] = None

    def flush() -> None:
        if cur is not None:
            records.append(_wifi_record(cur["mac"], cur["ssid"], cur["rssi"], cur["freq"]))

    for raw in text.splitlines():
        line = raw.strip()
        m = _BSS_RE.match(line)
        if m:
            flush()
            cur = {"mac": m.group(1), "ssid": "", "rssi": 0, "freq": 0}
            continue
        if cur is None:
            continue
        if line.startswith("signal:"):
            mm = re.search(r"signal:\s*(-?\d+(?:\.\d+)?)", line)
            if mm:
                cur["rssi"] = int(round(float(mm.group(1))))
        elif line.startswith("freq:"):
            mm = re.search(r"freq:\s*(\d+)", line)
            if mm:
                cur["freq"] = int(mm.group(1))
        elif line.startswith("SSID:"):
            cur["ssid"] = line[5:].strip()
    flush()
    return records


def ble_record(
    address: str,
    name: Optional[str],
    manufacturer_data: Optional[dict[int, bytes]],
    service_uuids: Optional[list[str]],
    rssi: Optional[int],
) -> Record:
    """Map a BLE advertisement to a detect Record (position 0/0)."""
    mfgrid = 0
    if manufacturer_data:
        mfgrid = next(iter(manufacturer_data.keys()))  # BT company id
    services = " ".join(service_uuids) if service_uuids else ""
    return {
        "mac": (address or "").lower(),
        "ssid": name or "",
        "type": "BLE",
        "rssi": int(rssi or 0),
        "lat": 0.0,
        "lon": 0.0,
        "first_seen": _now(),
        "services": services,
        "frequency": 0,
        "capabilities": "",
        "mfgrid": int(mfgrid),
    }


# ---------------------------------------------------------------------------
# Radio I/O (thin, lazy)
# ---------------------------------------------------------------------------

def wifi_scan_once(iface: str) -> list[Record]:
    """Run one `iw` active scan. Raises RuntimeError on missing tool / perms."""
    import shutil
    import subprocess

    if shutil.which("iw") is None:
        raise RuntimeError("`iw` not found — install it (e.g. sudo apt install iw)")
    try:
        proc = subprocess.run(
            ["iw", "dev", iface, "scan"], capture_output=True, text=True, timeout=30
        )
    except FileNotFoundError as exc:
        raise RuntimeError("`iw` not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f"iw scan on {iface} timed out") from exc
    if proc.returncode != 0:
        err = (proc.stderr or "").strip()
        raise RuntimeError(
            f"iw scan failed on {iface}: {err or 'permission denied?'} "
            "(needs root; try sudo or `sudo setcap cap_net_admin,cap_net_raw+eip $(which iw)`)"
        )
    return parse_iw_scan(proc.stdout)


def _wifi_thread(q: queue.Queue, iface: str, interval: float, should_stop: Callable[[], bool]) -> None:
    while not should_stop():
        try:
            recs = wifi_scan_once(iface)
        except RuntimeError as exc:
            q.put(("__error__", f"Wi-Fi: {exc}"))
            return
        for r in recs:
            q.put(("wifi", r))
        slept = 0.0
        while slept < interval and not should_stop():
            time.sleep(0.2)
            slept += 0.2


def _ble_thread(q: queue.Queue, should_stop: Callable[[], bool]) -> None:
    try:
        import asyncio

        from bleak import BleakScanner
    except ImportError:
        q.put(("__error__", "BLE: needs the 'pi' extra — pip install 'flockdar[pi]'"))
        return

    async def run() -> None:
        def cb(device: Any, adv: Any) -> None:
            q.put(("ble", ble_record(
                device.address, adv.local_name, adv.manufacturer_data,
                list(adv.service_uuids or []), adv.rssi,
            )))

        scanner = BleakScanner(detection_callback=cb)
        await scanner.start()
        try:
            while not should_stop():
                await asyncio.sleep(0.4)
        finally:
            await scanner.stop()

    try:
        asyncio.run(run())
    except Exception as exc:  # adapter missing / DBus error
        q.put(("__error__", f"BLE scan failed: {exc}"))


# ---------------------------------------------------------------------------
# Iterators
# ---------------------------------------------------------------------------

def iter_records(
    *,
    wifi: bool = True,
    ble: bool = True,
    wifi_iface: str = "wlan0",
    interval: float = 10.0,
    should_stop: Optional[Callable[[], bool]] = None,
    position_fn: Optional[Callable[[], Optional[Position]]] = None,
    on_status: Optional[Callable[[str], None]] = None,
) -> Iterator[tuple[Record, str]]:
    """Yield (record, kind) from the host's radios. kind is 'wifi' or 'ble'.

    `position_fn` (e.g. Meshtastic) stamps each detection. Scanner start-up
    failures are reported via `on_status` and don't stop the other scanner.
    """
    stop = should_stop or (lambda: False)
    q: queue.Queue = queue.Queue()
    threads: list[threading.Thread] = []
    if wifi:
        threads.append(threading.Thread(
            target=_wifi_thread, args=(q, wifi_iface, interval, stop), daemon=True))
    if ble:
        threads.append(threading.Thread(target=_ble_thread, args=(q, stop), daemon=True))
    if not threads:
        raise RuntimeError("native scan: enable at least one of wifi/ble")
    for t in threads:
        t.start()

    while True:
        if stop() and q.empty():
            return
        try:
            kind, payload = q.get(timeout=0.5)
        except queue.Empty:
            if all(not t.is_alive() for t in threads):
                return  # every scanner exited (e.g. all failed to start)
            continue
        if kind == "__error__":
            if on_status:
                on_status(payload)
            continue
        rec = payload
        if position_fn is not None:
            fix = position_fn()
            if fix is not None:
                rec["lat"], rec["lon"] = fix
        yield rec, kind


def iter_hits(
    *,
    wifi: bool = True,
    ble: bool = True,
    wifi_iface: str = "wlan0",
    interval: float = 10.0,
    should_stop: Optional[Callable[[], bool]] = None,
    position_fn: Optional[Callable[[], Optional[Position]]] = None,
    on_status: Optional[Callable[[str], None]] = None,
) -> Iterator[detect.Hit]:
    """Yield a Hit per detection (no dedup — see serial_import.merge_hit)."""
    for rec, kind in iter_records(
        wifi=wifi, ble=ble, wifi_iface=wifi_iface, interval=interval,
        should_stop=should_stop, position_fn=position_fn, on_status=on_status,
    ):
        hit = detect.analyze(**rec)
        if hit:
            hit.add_signal("PI_NATIVE", kind)
            yield hit


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Native Raspberry Pi Wi-Fi/BLE scan for Flock devices."
    )
    ap.add_argument("output", nargs="?", help="optional output .sqlite (TUI-openable)")
    ap.add_argument("--wifi-iface", default="wlan0", help="Wi-Fi interface (default wlan0)")
    ap.add_argument("--no-wifi", action="store_true", help="disable Wi-Fi scanning")
    ap.add_argument("--no-ble", action="store_true", help="disable BLE scanning")
    ap.add_argument("--interval", type=float, default=10.0, help="Wi-Fi rescan seconds")
    ap.add_argument(
        "--meshtastic", nargs="?", const="", metavar="DEV",
        help="take GPS from a Meshtastic node over serial (DEV, or blank to auto-detect)",
    )
    ap.add_argument("--meshtastic-host", metavar="HOST", help="Meshtastic node over TCP")
    ap.add_argument(
        "--flush-interval", type=float, default=30.0,
        help="when writing SQLite, flush at most this often (default 30)",
    )
    args = ap.parse_args(argv)

    wifi, ble = not args.no_wifi, not args.no_ble
    if not wifi and not ble:
        ap.error("nothing to scan: do not pass both --no-wifi and --no-ble")

    position_fn = None
    gps_src = None
    if args.meshtastic is not None or args.meshtastic_host:
        from . import gps_source

        gps_src = gps_source.open_meshtastic(
            dev_path=(args.meshtastic or None), hostname=args.meshtastic_host
        )
        position_fn = gps_src.current
        print("Using Meshtastic node for GPS position.", file=sys.stderr)

    import signal

    stop = {"flag": False}

    def _handle(signum, frame):  # noqa: ANN001
        stop["flag"] = True

    signal.signal(signal.SIGINT, _handle)
    signal.signal(signal.SIGTERM, _handle)

    print(
        f"Native scan: wifi={'on(' + args.wifi_iface + ')' if wifi else 'off'} "
        f"ble={'on' if ble else 'off'} — Ctrl-C to stop.",
        file=sys.stderr,
    )

    records: list[Record] = []
    last_flush = 0.0
    try:
        for rec, kind in iter_records(
            wifi=wifi, ble=ble, wifi_iface=args.wifi_iface, interval=args.interval,
            should_stop=lambda: stop["flag"], position_fn=position_fn,
            on_status=lambda msg: print(msg, file=sys.stderr),
        ):
            records.append(rec)
            hit = detect.analyze(**rec)
            if hit:
                hit.add_signal("PI_NATIVE", kind)
                print(serial_import._summary(hit, kind))
            if args.output:
                now = time.monotonic()
                if now - last_flush >= args.flush_interval:
                    serial_import.write_sqlite(records, args.output)
                    last_flush = now
    except KeyboardInterrupt:
        print("\nStopped.", file=sys.stderr)
    finally:
        if gps_src is not None:
            gps_src.close()

    if args.output:
        n = serial_import.write_sqlite(records, args.output)
        print(f"Wrote {n} record(s) -> {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
