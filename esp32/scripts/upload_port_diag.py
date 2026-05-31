#!/usr/bin/env python3
"""Check whether a T-Deck upload COM port is openable (Windows error 31 probe)."""
from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path

LOG = Path(__file__).resolve().parents[2] / "debug-upload-diag.log"


def log(message: str, data: dict) -> None:
    entry = {"timestamp": int(time.time() * 1000), "message": message, "data": data}
    with LOG.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry) + "\n")


def win_serial_ports() -> list[dict]:
    ps = (
        "Get-CimInstance Win32_SerialPort | "
        "Select-Object DeviceID, Name, Description | ConvertTo-Json -Compress"
    )
    try:
        out = subprocess.check_output(
            ["powershell", "-Command", ps], text=True, timeout=15
        ).strip()
        if not out:
            return []
        parsed = json.loads(out)
        return parsed if isinstance(parsed, list) else [parsed]
    except Exception as exc:
        return [{"error": str(exc)}]


def try_open_port(port: str) -> dict:
    try:
        import serial
    except ImportError:
        return {"port": port, "open_ok": False, "error": "pyserial not installed"}
    try:
        ser = serial.Serial(port, 115200, timeout=0.5)
        ser.close()
        return {"port": port, "open_ok": True}
    except Exception as exc:
        return {"port": port, "open_ok": False, "error": f"{type(exc).__name__}: {exc}"}


def main() -> int:
    target = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    ports = win_serial_ports()
    open_result = try_open_port(target)
    log("ports", {"ports": ports})
    log("open", open_result)
    print(f"open_ok={open_result.get('open_ok')}  log={LOG}")
    return 0 if open_result.get("open_ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
