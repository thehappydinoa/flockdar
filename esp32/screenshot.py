#!/usr/bin/env python3
"""Capture a PNG/BMP from a LilyGO T-Deck running flockdar t-deck firmware.

Default workflow avoids wiping your UI after USB open:

1. Run this script (it opens COM once and waits).
2. Navigate on the T-Deck to the screen you want.
3. Press ``p`` on the keyboard — capture starts without a host ``screenshot`` command.

Use ``--host`` only if you want the script to send ``screenshot`` over serial
(that runs right after boot and captures whatever is on screen then).

Examples:
    uv run python esp32/screenshot.py COM4 -o demo.bmp
    uv run python esp32/screenshot.py COM4 --host -o boot.bmp

Close PlatformIO monitor / flockdar --serial first. Opening COM may still reboot
the board once on Windows; device-trigger mode lets you navigate *after* that.
If USB does not reboot the board, pass ``--already-running`` or wait until the
script prints ``Listening…`` before pressing ``p`` (pressing ``p`` earlier stalls
the port with binary data).
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from pathlib import Path

try:
    from flockdar.serial_import import _serial_close, _serial_open
except ImportError as e:
    raise SystemExit("run from repo root: uv run python esp32/screenshot.py …") from e


def _rgb565_to_rgb888(data: bytes, swap_bytes: bool) -> bytes:
    """Decode RGB565; default (no swap) matches TFT_eSPI 16bpp sprite RAM byte order."""
    out = bytearray(len(data) // 2 * 3)
    o = 0
    for i in range(0, len(data), 2):
        c = data[i] | data[i + 1] << 8 if swap_bytes else data[i] << 8 | data[i + 1]
        out[o] = ((c >> 11) & 0x1F) * 255 // 31
        out[o + 1] = ((c >> 5) & 0x3F) * 255 // 63
        out[o + 2] = (c & 0x1F) * 255 // 31
        o += 3
    return bytes(out)


def _write_bmp(path: Path, width: int, height: int, rgb: bytes) -> None:
    row = ((width * 3 + 3) // 4) * 4
    pad = row - width * 3
    pixel_bytes = row * height
    hdr = struct.pack(
        "<2sIHHI",
        b"BM",
        14 + 40 + pixel_bytes,
        0,
        0,
        14 + 40,
    )
    dib = struct.pack(
        "<IIIHHIIIIII",
        40,
        width,
        height,
        1,
        24,
        0,
        pixel_bytes,
        2835,
        2835,
        0,
        0,
    )
    with path.open("wb") as f:
        f.write(hdr)
        f.write(dib)
        for y in range(height - 1, -1, -1):
            row_off = y * width * 3
            row_buf = bytearray(width * 3)
            for x in range(width):
                i = row_off + x * 3
                # BMP 24bpp rows are BGR (not RGB).
                row_buf[x * 3] = rgb[i + 2]
                row_buf[x * 3 + 1] = rgb[i + 1]
                row_buf[x * 3 + 2] = rgb[i]
            f.write(row_buf)
            if pad:
                f.write(b"\x00" * pad)


def _write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    try:
        from PIL import Image
    except ImportError as e:
        raise SystemExit("PNG output needs Pillow: uv pip install pillow") from e
    img = Image.frombytes("RGB", (width, height), rgb)
    img.save(path)


def _read_json_line(ser, timeout_s: float) -> dict | None:
    deadline = time.monotonic() + timeout_s
    buf = bytearray()
    while time.monotonic() < deadline:
        chunk = ser.read(1)
        if not chunk:
            continue
        if chunk == b"\n":
            break
        buf.extend(chunk)
    if not buf:
        return None
    try:
        return json.loads(buf.decode("utf-8", errors="replace"))
    except json.JSONDecodeError:
        return None


def _wait_device_ready(
    ser, *, timeout_s: float = 25.0, already_running: bool
) -> tuple[dict | None, bool]:
    """Wait for firmware after USB open.

    Returns ``(screenshot_meta, fresh_boot)``.
    * ``screenshot_meta`` — header JSON if the device already started capture.
    * ``fresh_boot`` — True only after a post-reset ``online`` line (safe to flush RX).
    """
    if already_running:
        print("Skipping boot wait (--already-running)", file=sys.stderr)
        time.sleep(0.2)
        return None, False

    print("Waiting for device (USB open may reboot the board)…", file=sys.stderr)
    print("  Do not press p until you see 'Listening…'", file=sys.stderr)
    deadline = time.monotonic() + timeout_s
    saw_activity = False
    while time.monotonic() < deadline:
        meta = _read_json_line(ser, timeout_s=min(0.5, deadline - time.monotonic()))
        if meta is None:
            continue
        saw_activity = True
        msg = str(meta.get("msg", "") or "")
        kind = meta.get("type", "?")
        if kind == "screenshot":
            print(
                "Screenshot header received (device pressed p early) — receiving now",
                file=sys.stderr,
            )
            return meta, False
        if kind == "info" and msg.startswith("online"):
            print(f"device ready: {msg}", file=sys.stderr)
            time.sleep(0.8)
            return None, True
        if kind == "info":
            print(f"device alive: {msg}", file=sys.stderr)
            time.sleep(0.3)
            return None, False
        print(f"boot {kind}: {msg}" if msg else f"boot {kind}", file=sys.stderr)

    if not saw_activity:
        raise RuntimeError(
            "no serial JSON from device — check COM port, close PlatformIO monitor / "
            "flockdar --serial, reflash t-deck firmware, then retry"
        )
    print(
        "No 'online' (board was already running) — continuing; use --already-running "
        "next time to skip this wait",
        file=sys.stderr,
    )
    return None, False


_SCREENSHOT_MAGIC = b"FDSC"


def _strip_json_prefix(raw: bytearray) -> int:
    """Drop complete JSON lines accidentally emitted after FDSC (legacy firmware)."""
    skipped = 0
    while True:
        nl = raw.find(b"\n")
        if nl < 0 or nl > 512:
            break
        line = raw[:nl]
        if not (line.startswith(b"{") and line.endswith(b"}")):
            break
        try:
            json.loads(line.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            break
        del raw[: nl + 1]
        skipped += nl + 1
    return skipped


def _read_binary_after_magic(ser, nbytes: int, timeout_s: float) -> bytes:
    """Sync to FDSC marker (skips stray JSON/text) then read nbytes of RGB565."""
    buf = bytearray()
    deadline = time.monotonic() + timeout_s
    magic_at = -1
    while magic_at < 0 and time.monotonic() < deadline:
        chunk = ser.read(512)
        if chunk:
            buf.extend(chunk)
            magic_at = buf.find(_SCREENSHOT_MAGIC)
        if len(buf) > 65536:
            buf = buf[-4:]
    if magic_at < 0:
        raise RuntimeError("timeout waiting for FDSC marker before screenshot pixels")
    raw = bytearray(buf[magic_at + len(_SCREENSHOT_MAGIC) :])
    skipped = _strip_json_prefix(raw)
    if skipped:
        print(f"  skipped {skipped} bytes of stray JSON after FDSC", file=sys.stderr)
    last_report = 0
    while len(raw) < nbytes and time.monotonic() < deadline:
        want = min(4096, nbytes - len(raw))
        chunk = ser.read(want) if want > 0 else b""
        if chunk:
            raw.extend(chunk)
            if len(raw) - last_report >= 8192 or len(raw) == nbytes:
                pct = 100 * len(raw) // nbytes
                print(
                    f"\r  {len(raw)} / {nbytes} bytes ({pct}%)",
                    end="",
                    file=sys.stderr,
                )
                last_report = len(raw)
        elif time.monotonic() >= deadline:
            break
    print(file=sys.stderr)
    if len(raw) < nbytes:
        raise RuntimeError(
            f"short read: got {len(raw)} of {nbytes} bytes after FDSC sync"
        )
    return bytes(raw[:nbytes])


def _wait_screenshot_header(ser, timeout_s: float) -> dict:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        meta = _read_json_line(ser, timeout_s=min(2.0, deadline - time.monotonic()))
        if meta is None:
            continue
        if meta.get("type") == "screenshot":
            return meta
        kind = meta.get("type", "?")
        msg = meta.get("msg", "")
        print(f"skip {kind}: {msg}" if msg else f"skip {kind}", file=sys.stderr)
    raise RuntimeError(
        "timeout waiting for screenshot header — reflash t-deck firmware with "
        "screenshot support, or check the device did not reboot again"
    )


def capture(
    port: str,
    *,
    baud: int = 115200,
    out: Path,
    timeout_s: float = 180.0,
    swap_bytes: bool = False,
    already_running: bool = False,
    host_trigger: bool = False,
) -> Path:
    ser = _serial_open(port, baud)
    ser.timeout = 0.25
    try:
        early_meta, fresh_boot = _wait_device_ready(
            ser, already_running=already_running
        )
        if fresh_boot:
            ser.reset_input_buffer()
        if early_meta is not None:
            meta = early_meta
        elif host_trigger:
            ser.write(b"screenshot\n")
            ser.flush()
            print("Host sent screenshot (current screen)", file=sys.stderr)
            meta = _wait_screenshot_header(ser, timeout_s=timeout_s)
        else:
            print(
                "Listening… navigate on the T-Deck, then press p to capture "
                f"(timeout {timeout_s:.0f}s).",
                file=sys.stderr,
            )
            meta = _wait_screenshot_header(ser, timeout_s=timeout_s)
        w = int(meta["w"])
        h = int(meta["h"])
        nbytes = int(meta.get("len", w * h * 2))
        print(
            f"Receiving {w}x{h} RGB565 ({nbytes} bytes, ~{nbytes * 10 // 115200}s at 115200)…",
            file=sys.stderr,
        )
        pitch = int(meta.get("pitch", w))
        if pitch != w:
            print(f"  sprite row pitch {pitch}px (visible width {w})", file=sys.stderr)
        raw = _read_binary_after_magic(ser, nbytes, timeout_s=timeout_s)
        _ = ser.readline()
    finally:
        _serial_close(ser)

    rgb = _rgb565_to_rgb888(bytes(raw), swap_bytes=swap_bytes)
    if out.suffix.lower() == ".png":
        _write_png(out, w, h, rgb)
    else:
        if out.suffix.lower() not in (".bmp", ""):
            out = out.with_suffix(".bmp")
        _write_bmp(out, w, h, rgb)
    print(f"Wrote {out}", file=sys.stderr)
    return out


def main() -> None:
    p = argparse.ArgumentParser(description="Capture T-Deck screenshot over serial")
    p.add_argument("port", help="Serial port (e.g. COM4, /dev/ttyACM0)")
    p.add_argument("-o", "--output", type=Path, default=Path("tdeck-screenshot.bmp"))
    p.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Serial baud (firmware default 115200; USB-CDC often ignores this)",
    )
    p.add_argument("--timeout", type=float, default=180.0)
    p.add_argument(
        "--host",
        action="store_true",
        help="Send screenshot command from PC (captures soon after USB open)",
    )
    p.add_argument(
        "--already-running",
        action="store_true",
        help="Skip boot wait (USB open did not reboot the board)",
    )
    p.add_argument(
        "--swap-bytes",
        action="store_true",
        help="Decode RGB565 as little-endian (default: native TFT_eSPI sprite RAM byte order)",
    )
    args = p.parse_args()
    if args.baud != 115200:
        print(
            "warning: firmware uses 115200; use default unless you have a UART bridge",
            file=sys.stderr,
        )
    capture(
        args.port,
        baud=args.baud,
        out=args.output,
        timeout_s=args.timeout,
        swap_bytes=args.swap_bytes,
        already_running=args.already_running,
        host_trigger=args.host,
    )


if __name__ == "__main__":
    main()
