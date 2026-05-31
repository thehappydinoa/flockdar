"""Tests for serial_import.py — HMAC verify, record mapping, NDJSON ingest."""

from __future__ import annotations

import hmac
from hashlib import sha256

from flockdar import detect
from flockdar import serial_import as si

KEY = b"test-key"


def sign(payload: str) -> str:
    """Build an emitted firmware line: object + inserted sig (as the firmware does)."""
    sig = hmac.new(KEY, payload.encode(), sha256).hexdigest()[:8]
    return payload[:-1] + f',"sig":"{sig}"}}'


WIFI_PAYLOAD = (
    '{"v":1,"type":"wifi","method":"addr2","mac":"70:c9:4e:00:00:01",'
    '"oui":"70:c9:4e","rssi":-72,"channel":6,"ts_ms":123}'
)
BLE_PAYLOAD = (
    '{"v":1,"type":"ble","method":"mfgrid","mac":"d4:b2:73:00:00:01",'
    '"name":"Penguin-1069698414","mfgrid":2504,"rssi":-90,"ts_ms":456}'
)


class TestVerifyLine:
    def test_valid_signature(self) -> None:
        assert si.verify_line(sign(WIFI_PAYLOAD), KEY)

    def test_trailing_newline_ok(self) -> None:
        assert si.verify_line(sign(WIFI_PAYLOAD) + "\n", KEY)

    def test_tampered_body_fails(self) -> None:
        line = sign(WIFI_PAYLOAD).replace("-72", "-30")
        assert not si.verify_line(line, KEY)

    def test_wrong_key_fails(self) -> None:
        assert not si.verify_line(sign(WIFI_PAYLOAD), b"other-key")

    def test_missing_sig_fails(self) -> None:
        assert not si.verify_line(WIFI_PAYLOAD, KEY)


class TestLineToRecord:
    def test_wifi_channel_to_mhz(self) -> None:
        rec, method = si.line_to_record(
            {"type": "wifi", "method": "addr1", "mac": "02:00:00:00:00:01",
             "rssi": -60, "channel": 6},
            {"lat": 0.0, "lon": 0.0},
        )
        assert rec["type"] == "WIFI"
        assert rec["frequency"] == 2437  # ch 6
        assert method == "addr1"

    def test_ble_name_and_mfgrid(self) -> None:
        rec, _ = si.line_to_record(
            {"type": "ble", "method": "mfgrid", "mac": "02:00:00:00:00:01",
             "name": "FS Ext Battery", "mfgrid": 2504, "rssi": -88},
            {"lat": 0.0, "lon": 0.0},
        )
        assert rec["type"] == "BLE"
        assert rec["ssid"] == "FS Ext Battery"
        assert rec["mfgrid"] == 2504

    def test_position_inherited(self) -> None:
        rec, _ = si.line_to_record(
            {"type": "wifi", "mac": "02:00:00:00:00:01"},
            {"lat": 40.1, "lon": -74.1},
        )
        assert rec["lat"] == 40.1 and rec["lon"] == -74.1

    def test_inline_position_overrides_running(self) -> None:
        rec, _ = si.line_to_record(
            {"type": "wifi", "mac": "02:00:00:00:00:01",
             "lat": 40.0016, "lon": -74.0008, "accuracy": 3.1},
            {"lat": 40.0, "lon": -74.0},
        )
        assert rec["lat"] == 40.0016 and rec["lon"] == -74.0008

    def test_non_detection_returns_none(self) -> None:
        assert si.line_to_record({"type": "info"}, {}) is None


class TestIterStream:
    def test_gps_updates_position(self) -> None:
        lines = [
            '{"v":1,"type":"gps","lat":40.0,"lon":-74.0,"alt":1,"accuracy":3,"ts_ms":1}',
            sign(WIFI_PAYLOAD),
        ]
        recs = list(si.iter_records(lines, KEY))
        assert len(recs) == 1
        rec, _ = recs[0]
        assert rec["lat"] == 40.0 and rec["lon"] == -74.0

    def test_forged_line_skipped(self) -> None:
        good = sign(WIFI_PAYLOAD)
        forged = good.replace("-72", "-30")
        recs = list(si.iter_records([good, forged], KEY))
        assert len(recs) == 1

    def test_verification_can_be_disabled(self) -> None:
        recs = list(si.iter_records([WIFI_PAYLOAD], KEY, verify=False))
        assert len(recs) == 1

    def test_iter_hits_chip_oui(self) -> None:
        hits = list(si.iter_hits([sign(WIFI_PAYLOAD)], KEY))
        assert len(hits) == 1
        labels = {lbl for lbl, _ in hits[0].signals}
        assert "CHIP_OUI" in labels
        assert "ESP32_LIVE" in labels  # provenance signal carries the method

    def test_iter_hits_penguin_ble(self) -> None:
        hits = list(si.iter_hits([sign(BLE_PAYLOAD)], KEY))
        assert hits and hits[0].confidence == 3  # PENGUIN_BLE_SSID


class TestMergeHit:
    def test_dedup_merges_signals_and_max_rssi(self) -> None:
        seen: dict[str, detect.Hit] = {}
        h1 = si.iter_hits([sign(WIFI_PAYLOAD)], KEY).__next__()
        assert si.merge_hit(seen, h1) is True
        h2 = si.iter_hits([sign(WIFI_PAYLOAD.replace("-72", "-50"))], KEY).__next__()
        assert si.merge_hit(seen, h2) is False  # same MAC
        assert len(seen) == 1
        assert seen[h1.mac.lower()].rssi == -50  # kept the stronger signal


class TestLogAndSqlite:
    def test_load_log_and_sqlite_roundtrip(self, tmp_path) -> None:
        log = tmp_path / "flock.ndjson"
        log.write_text("\n".join([sign(WIFI_PAYLOAD), sign(BLE_PAYLOAD)]) + "\n")

        hits, total = si.load_log(log, KEY)
        assert total == 2
        assert len(hits) == 2

        # Records -> WiGLE-format sqlite the TUI/detect can re-open.
        out = tmp_path / "out.sqlite"
        recs = [rec for rec, _ in si.iter_records(si.log_lines(log), KEY)]
        assert si.write_sqlite(recs, str(out)) == 2

        reread, scanned = detect.run_detection(out)
        assert scanned == 2
        macs = {h.mac.lower() for h in reread}
        assert "70:c9:4e:00:00:01" in macs


class TestSourceDetection:
    def test_com_port_is_serial(self) -> None:
        assert si.is_serial_source("COM3")

    def test_existing_file_is_log(self, tmp_path) -> None:
        f = tmp_path / "x.ndjson"
        f.write_text("{}")
        assert not si.is_serial_source(str(f))

    def test_missing_path_is_serial(self) -> None:
        assert si.is_serial_source("/dev/ttyUSB0")
