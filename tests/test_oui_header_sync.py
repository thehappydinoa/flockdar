"""oui_list.h / vendor_list.h must match gen_oui_header.py output."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ESP32_DIR = Path(__file__).resolve().parents[1] / "esp32"
GEN = ESP32_DIR / "gen_oui_header.py"
OUI = ESP32_DIR / "oui_list.h"
VENDOR = ESP32_DIR / "vendor_list.h"


def test_oui_header_in_sync() -> None:
    oui_before = OUI.read_text(encoding="utf-8")
    vendor_before = VENDOR.read_text(encoding="utf-8")
    subprocess.run(
        [sys.executable, str(GEN)],
        check=True,
        cwd=ESP32_DIR.parent,
    )
    assert OUI.read_text(encoding="utf-8") == oui_before, (
        "esp32/oui_list.h is stale — run `uv run esp32/gen_oui_header.py`"
    )
    assert VENDOR.read_text(encoding="utf-8") == vendor_before, (
        "esp32/vendor_list.h is stale — run `uv run esp32/gen_oui_header.py`"
    )
