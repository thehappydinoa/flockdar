"""Tests for scan_native.py — Pi Wi-Fi (iw parse) and BLE record mapping.

The pure mappers are tested directly; the threaded iter_hits path is exercised
with a monkeypatched `wifi_scan_once` (no real radio, BLE disabled).
"""

from __future__ import annotations

from flockdar import detect
from flockdar import scan_native as sn

IW_SAMPLE = """\
BSS 70:c9:4e:79:e7:66(on wlan0)
\tlast seen: 100 ms ago
\tfreq: 2412
\tsignal: -45.00 dBm
\tSSID: Flock-79E766
\tcapability: ESS Privacy (0x0411)
BSS aa:bb:cc:dd:ee:ff(on wlan0)
\tfreq: 2437
\tsignal: -71.00 dBm
\tSSID: HomeNetwork
BSS b4:1e:52:00:11:22(on wlan0)
\tfreq: 5180
\tsignal: -60.00 dBm
\tSSID:
"""


class TestParseIwScan:
    def test_parses_all_bss(self) -> None:
        recs = sn.parse_iw_scan(IW_SAMPLE)
        assert len(recs) == 3

    def test_fields(self) -> None:
        rec = sn.parse_iw_scan(IW_SAMPLE)[0]
        assert rec["mac"] == "70:c9:4e:79:e7:66"
        assert rec["ssid"] == "Flock-79E766"
        assert rec["rssi"] == -45
        assert rec["frequency"] == 2412
        assert rec["type"] == "WIFI"

    def test_hidden_ssid_empty(self) -> None:
        rec = sn.parse_iw_scan(IW_SAMPLE)[2]
        assert rec["ssid"] == "" and rec["mac"] == "b4:1e:52:00:11:22"

    def test_empty_input(self) -> None:
        assert sn.parse_iw_scan("") == []

    def test_record_feeds_detection(self) -> None:
        # The Flock camera SSID record should analyze to a HIGH hit.
        rec = sn.parse_iw_scan(IW_SAMPLE)[0]
        hit = detect.analyze(**rec)
        assert hit is not None and hit.confidence == 3


class TestBleRecord:
    def test_manufacturer_id_becomes_mfgrid(self) -> None:
        rec = sn.ble_record("AA:BB:CC:DD:EE:FF", "x", {2504: b"\x01\x02"}, [], -88)
        assert rec["mfgrid"] == 2504 and rec["type"] == "BLE"
        assert rec["mac"] == "aa:bb:cc:dd:ee:ff"

    def test_service_uuids_joined(self) -> None:
        uuids = ["00003100-0000-1000-8000-00805f9b34fb"]
        rec = sn.ble_record("aa:bb:cc:dd:ee:ff", None, None, uuids, -70)
        assert rec["services"] == uuids[0]
        # Raven high-confidence service UUID -> HIGH hit.
        hit = detect.analyze(**rec)
        assert hit is not None and hit.confidence == 3

    def test_penguin_name(self) -> None:
        rec = sn.ble_record("aa:bb:cc:dd:ee:ff", "Penguin-1069698414", {2504: b""}, [], -90)
        hit = detect.analyze(**rec)
        assert hit is not None and hit.confidence == 3  # PENGUIN_BLE_SSID

    def test_no_manufacturer_data(self) -> None:
        rec = sn.ble_record("aa:bb:cc:dd:ee:ff", "FS Ext Battery", None, None, -80)
        assert rec["mfgrid"] == 0
        hit = detect.analyze(**rec)
        assert hit is not None  # BLE_NAME match


class TestIterHits:
    def test_wifi_native_hit(self, monkeypatch) -> None:
        flock = sn.parse_iw_scan(IW_SAMPLE)[0]
        monkeypatch.setattr(sn, "wifi_scan_once", lambda iface: [flock])
        gen = sn.iter_hits(
            wifi=True, ble=False, wifi_iface="wlan0", interval=0.05,
            should_stop=lambda: False,
        )
        try:
            hit = next(gen)
        finally:
            gen.close()
        labels = {lbl for lbl, _ in hit.signals}
        assert "PI_NATIVE" in labels
        assert hit.mac == "70:c9:4e:79:e7:66"

    def test_position_fn_stamps_native_hit(self, monkeypatch) -> None:
        flock = sn.parse_iw_scan(IW_SAMPLE)[0]
        monkeypatch.setattr(sn, "wifi_scan_once", lambda iface: [flock])
        gen = sn.iter_hits(
            wifi=True, ble=False, interval=0.05,
            should_stop=lambda: False, position_fn=lambda: (39.9416, -75.1758),
        )
        try:
            hit = next(gen)
        finally:
            gen.close()
        assert (hit.lat, hit.lon) == (39.9416, -75.1758)

    def test_nothing_to_scan_raises(self) -> None:
        import pytest

        with pytest.raises(RuntimeError):
            next(sn.iter_records(wifi=False, ble=False))
