"""Validate the NMEA command checksums hardcoded in the ESP32 GPS firmware.

The T-Deck L76K is configured with literal `$...*HH` NMEA strings in
esp32/src/gps.cpp. A wrong checksum makes the module silently reject the
command — which previously left the GPS "stuck at acquiring". This parses every
such literal out of the source and recomputes its checksum so a typo fails CI
without needing hardware.
"""

from __future__ import annotations

import re
from pathlib import Path

GPS_CPP = Path(__file__).resolve().parents[1] / "esp32" / "src" / "gps.cpp"

# Matches "$PCAS04,7*1E" inside a C string literal.
_NMEA_RE = re.compile(r'\$([A-Z0-9,.\-]+)\*([0-9A-Fa-f]{2})')


def _checksum(body: str) -> str:
    x = 0
    for ch in body:
        x ^= ord(ch)
    return f"{x:02X}"


def _commands() -> list[tuple[str, str]]:
    text = GPS_CPP.read_text(encoding="utf-8")
    return _NMEA_RE.findall(text)


def test_found_expected_commands() -> None:
    bodies = [b for b, _ in _commands()]
    # The worldwide GNSS-mode command and RMC+GGA output config must be present.
    assert any(b.startswith("PCAS04,7") for b in bodies), "expected PCAS04,7 (worldwide)"
    assert any(b.startswith("PCAS03,") for b in bodies), "expected a PCAS03 output config"
    assert any(b.startswith("PCAS10,3") for b in bodies), "expected factory reset"
    assert any(b.startswith("PCAS02,1000") for b in bodies), "expected 1 Hz rate"
    assert any(b.startswith("PUBX,40,GGA") for b in bodies), "expected u-blox PUBX GGA"
    assert len(bodies) >= 10


def test_all_nmea_checksums_valid() -> None:
    bad = [
        f"$ {body}*{given} (correct *{_checksum(body)})"
        for body, given in _commands()
        if given.upper() != _checksum(body)
    ]
    assert not bad, "Wrong NMEA checksum(s) in gps.cpp:\n" + "\n".join(bad)


def test_no_gps_glonass_only_mode() -> None:
    # Regression: PCAS04,5 (GPS+GLONASS, no BeiDou) was the slow-acquisition bug.
    bodies = [b for b, _ in _commands()]
    assert not any(b.strip() == "PCAS04,5" for b in bodies), \
        "PCAS04,5 drops BeiDou — use ,7 for worldwide coverage"
