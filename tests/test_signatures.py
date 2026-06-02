"""Tests for signatures.py — regex patterns, OUI sets, UUID sets."""

from __future__ import annotations

import re

from flockdar import signatures as sig


class TestFlockCameraSSIDRe:
    def test_matches_title_case(self) -> None:
        assert sig.FLOCK_CAMERA_SSID_RE.match("Flock-7F68FF")

    def test_matches_upper_case(self) -> None:
        assert sig.FLOCK_CAMERA_SSID_RE.match("FLOCK-A4E7B7")

    def test_matches_lowercase_hex(self) -> None:
        assert sig.FLOCK_CAMERA_SSID_RE.match("Flock-abcdef")

    def test_no_match_missing_suffix(self) -> None:
        assert not sig.FLOCK_CAMERA_SSID_RE.match("Flock-")

    def test_no_match_too_short(self) -> None:
        assert not sig.FLOCK_CAMERA_SSID_RE.match("Flock-1234")

    def test_no_match_too_long(self) -> None:
        assert not sig.FLOCK_CAMERA_SSID_RE.match("Flock-1234567")

    def test_no_match_non_hex(self) -> None:
        assert not sig.FLOCK_CAMERA_SSID_RE.match("Flock-GGGGGG")

    def test_no_match_no_prefix(self) -> None:
        assert not sig.FLOCK_CAMERA_SSID_RE.match("7F68FF")

    def test_no_match_wrong_separator(self) -> None:
        assert not sig.FLOCK_CAMERA_SSID_RE.match("Flock_7F68FF")


class TestPenguinBLESSIDRe:
    def test_matches_10_digits(self) -> None:
        assert sig.PENGUIN_BLE_SSID_RE.match("Penguin-1069698414")

    def test_no_match_hex_suffix(self) -> None:
        assert not sig.PENGUIN_BLE_SSID_RE.match("Penguin-ABCDEF1234")

    def test_no_match_9_digits(self) -> None:
        assert not sig.PENGUIN_BLE_SSID_RE.match("Penguin-123456789")

    def test_no_match_11_digits(self) -> None:
        assert not sig.PENGUIN_BLE_SSID_RE.match("Penguin-12345678901")

    def test_no_match_no_prefix(self) -> None:
        assert not sig.PENGUIN_BLE_SSID_RE.match("1069698414")


class TestOUIsets:
    def test_direct_oui_not_in_chip_ouis(self) -> None:
        overlap = sig.FLOCK_DIRECT_OUIS & sig.FLOCK_CHIP_OUIS
        assert not overlap, f"OUIs in both sets: {overlap}"

    def test_oui_format(self) -> None:
        oui_pattern = re.compile(r"^[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}$")
        for oui in sig.FLOCK_DIRECT_OUIS | sig.FLOCK_CHIP_OUIS | sig.FLOCK_BACKHAUL_OUIS:
            assert oui_pattern.match(oui), f"Bad OUI format: {oui!r}"

    def test_all_ouis_covers_both_sets(self) -> None:
        assert sig.FLOCK_DIRECT_OUIS.issubset(sig.ALL_OUIS)
        assert sig.FLOCK_CHIP_OUIS.issubset(sig.ALL_OUIS)

    def test_known_direct_oui_present(self) -> None:
        assert "b4:1e:52" in sig.FLOCK_DIRECT_OUIS

    def test_known_camera_ouis_present(self) -> None:
        for oui in ("70:c9:4e", "08:3a:88", "d8:f3:bc"):
            assert oui in sig.FLOCK_CHIP_OUIS

    def test_eero_in_backhaul(self) -> None:
        assert "80:da:13" in sig.FLOCK_BACKHAUL_OUIS


class TestRavenServiceUUIDs:
    _uuid_re = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")

    def test_high_confidence_uuids_format(self) -> None:
        for uuid in sig.RAVEN_SERVICES_HIGH:
            assert self._uuid_re.match(uuid), f"Bad UUID: {uuid!r}"

    def test_custom_range_not_in_standard(self) -> None:
        # Raven custom UUIDs 0x3100–0x3500 should not appear in old (standard BT SIG) set
        assert not (sig.RAVEN_SERVICES_HIGH & sig.RAVEN_SERVICES_OLD)

    def test_known_high_uuids(self) -> None:
        assert "00003100-0000-1000-8000-00805f9b34fb" in sig.RAVEN_SERVICES_HIGH
        assert "00003500-0000-1000-8000-00805f9b34fb" in sig.RAVEN_SERVICES_HIGH

    def test_mfgrid_penguin(self) -> None:
        assert 2504 in sig.FLOCK_MFGRIDS

    def test_wifi_capab_string(self) -> None:
        assert "WPA2-PSK" in sig.FLOCK_WIFI_CAPAB
        assert "WPS" not in sig.FLOCK_WIFI_CAPAB

    def test_camera_channels(self) -> None:
        assert {2412, 2437, 2462} == sig.FLOCK_CAMERA_CHANNELS_MHZ
