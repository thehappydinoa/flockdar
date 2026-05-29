"""Shared fixtures for the flock-wigle-detect test suite."""

from __future__ import annotations

import zipfile
from io import BytesIO
from pathlib import Path

import pytest

from detect import Hit


# ---------------------------------------------------------------------------
# Hit factories
# ---------------------------------------------------------------------------

def make_hit(
    mac: str = "aa:bb:cc:dd:ee:ff",
    ssid: str = "",
    device_type: str = "W",
    lat: float = 39.94,
    lon: float = -75.17,
    rssi: int = -70,
    first_seen: str = "2026-04-28 16:00:00",
    services: str = "",
    frequency: int = 2412,
    capabilities: str = "",
    mfgrid: int = 0,
) -> Hit:
    return Hit(
        mac=mac,
        ssid=ssid,
        device_type=device_type,
        lat=lat,
        lon=lon,
        rssi=rssi,
        first_seen=first_seen,
        services=services,
        frequency=frequency,
        capabilities=capabilities,
        mfgrid=mfgrid,
    )


@pytest.fixture
def flocknet_hit() -> Hit:
    return make_hit(
        mac="80:da:13:79:e7:66",
        ssid="flocknet",
        device_type="W",
        lat=39.9416,
        lon=-75.1758,
    )


@pytest.fixture
def flock_camera_hit() -> Hit:
    """Flock camera with MAC-validated SSID: Flock-E766 suffix matches mac."""
    return make_hit(
        mac="70:c9:4e:79:e7:66",
        ssid="Flock-79E766",
        device_type="W",
        frequency=2412,
        capabilities="[WPA2-PSK-CCMP-128][RSN-PSK-CCMP-128][ESS]",
    )


@pytest.fixture
def penguin_ble_hit() -> Hit:
    return make_hit(
        mac="cc:09:24:20:da:ef",
        ssid="Penguin-1069698414",
        device_type="E",
        lat=27.5,
        lon=-82.3,
    )


@pytest.fixture
def hit_no_location() -> Hit:
    return make_hit(lat=0.0, lon=0.0)


# ---------------------------------------------------------------------------
# KML/KMZ helpers
# ---------------------------------------------------------------------------

def make_kml(placemarks: list[tuple[float, float, str]]) -> bytes:
    """Generate a minimal KML document with the given (lat, lon, name) placemarks."""
    # Build inline — no indentation so textwrap.dedent ambiguity doesn't arise
    # and the <?xml declaration is guaranteed to be at byte 0.
    pms = "".join(
        f"<Placemark>"
        f"<name>{name}</name>"
        f"<Point><coordinates>{lon},{lat},0</coordinates></Point>"
        f"</Placemark>"
        for lat, lon, name in placemarks
    )
    return (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<kml xmlns="http://www.opengis.net/kml/2.2">'
        f"<Document>{pms}</Document>"
        "</kml>"
    ).encode("utf-8")


def make_kmz(placemarks: list[tuple[float, float, str]]) -> bytes:
    """Wrap KML in a KMZ (ZIP) archive, returned as bytes."""
    buf = BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("doc.kml", make_kml(placemarks))
    return buf.getvalue()


@pytest.fixture
def sample_kmz_path(tmp_path: Path) -> Path:
    """A KMZ with three known ALPR locations."""
    cameras = [
        (39.9416, -75.1758, "Flock Camera A"),
        (39.9500, -75.1800, "Flock Camera B"),
        (40.0000, -76.0000, "Flock Camera C"),
    ]
    kmz = tmp_path / "test.kmz"
    kmz.write_bytes(make_kmz(cameras))
    return kmz
