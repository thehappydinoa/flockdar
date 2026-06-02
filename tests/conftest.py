"""Shared fixtures for the flock-wigle-detect test suite."""

from __future__ import annotations

import zipfile
from io import BytesIO
from pathlib import Path

import pytest

from flockdar.detect import Hit

# Fictional example coordinates (tests + docs). Not real wardrive locations.
EX_LAT = 40.0
EX_LON = -74.0
EX_LAT_A = 40.0016
EX_LON_A = -74.0008
EX_LAT_B = 40.0100
EX_LON_B = -74.0100
EX_LAT_C = 40.1000
EX_LON_C = -75.0000

# Fictional MACs — real Flock OUIs where needed for signature tests, zeroed suffixes.
MAC_EX = "02:00:00:00:00:01"
MAC_EX2 = "02:00:00:00:00:02"
MAC_FLOCK_CAM = "70:c9:4e:00:00:01"
SSID_FLOCK_CAM = "Flock-000001"
MAC_FLOCK_CHIP = "70:c9:4e:00:00:02"
MAC_EERO = "80:da:13:00:00:01"
MAC_EERO_HIDDEN = "80:da:13:00:00:02"
MAC_EERO_NAMED = "80:da:13:aa:bb:cc"
MAC_PENGUIN = "cc:09:24:00:00:01"
MAC_MFGRID = "d4:b2:73:00:00:01"
MAC_BLE_NAME = "04:0d:84:00:00:01"
MAC_SURVEILLANCE = "58:8e:81:00:00:01"
MAC_DIRECT = "b4:1e:52:00:00:01"
MAC_RAVEN = "00:40:8c:00:00:01"
MAC_UNKNOWN = "de:ad:be:ef:00:01"
MAC_WIFI_OTHER = "d8:f3:bc:00:00:01"


# ---------------------------------------------------------------------------
# Hit factories
# ---------------------------------------------------------------------------


def make_hit(
    mac: str = MAC_EX,
    ssid: str = "",
    device_type: str = "W",
    lat: float = EX_LAT,
    lon: float = EX_LON,
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
        mac=MAC_EERO,
        ssid="flocknet",
        device_type="W",
        lat=EX_LAT_A,
        lon=EX_LON_A,
    )


@pytest.fixture
def flock_camera_hit() -> Hit:
    """Flock camera with MAC-validated SSID."""
    return make_hit(
        mac=MAC_FLOCK_CAM,
        ssid=SSID_FLOCK_CAM,
        device_type="W",
        frequency=2412,
        capabilities="[WPA2-PSK-CCMP-128][RSN-PSK-CCMP-128][ESS]",
    )


@pytest.fixture
def penguin_ble_hit() -> Hit:
    return make_hit(
        mac=MAC_PENGUIN,
        ssid="Penguin-1069698414",
        device_type="E",
        lat=EX_LAT,
        lon=EX_LON,
    )


@pytest.fixture
def hit_no_location() -> Hit:
    return make_hit(lat=0.0, lon=0.0)


# ---------------------------------------------------------------------------
# KML/KMZ helpers
# ---------------------------------------------------------------------------


def make_kml(placemarks: list[tuple[float, float, str]]) -> bytes:
    """Generate a minimal KML document with the given (lat, lon, name) placemarks."""
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
    ).encode()


def make_kmz(placemarks: list[tuple[float, float, str]]) -> bytes:
    """Wrap KML in a ZIP archive, returned as bytes."""
    buf = BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("doc.kml", make_kml(placemarks))
    return buf.getvalue()


@pytest.fixture
def sample_kmz_path(tmp_path: Path) -> Path:
    """A KMZ with three known ALPR locations."""
    cameras = [
        (EX_LAT_A, EX_LON_A, "Flock Camera A"),
        (EX_LAT_B, EX_LON_B, "Flock Camera B"),
        (EX_LAT_C, EX_LON_C, "Flock Camera C"),
    ]
    kmz = tmp_path / "test.kmz"
    kmz.write_bytes(make_kmz(cameras))
    return kmz
