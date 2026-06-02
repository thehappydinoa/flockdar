"""BLE AD walker (mirrors ble_scanner.cpp ad_walk)."""

from __future__ import annotations

from tests.esp32.ble_ad import ad_walk, parse_ble_ad
from tests.esp32.match_parity import ble_name_match, mfgrid_is_flock


def test_malformed_ad_stops() -> None:
    payload = bytes([0, 0x09, ord("x")])  # field_len 0
    assert not ad_walk(payload, lambda _t, _d: True)


def test_name_and_mfgrid() -> None:
    # AD len includes type byte: [3, 0x09, 'H','i'] + [4, 0xFF, cid lo, cid hi]
    payload = bytes([3, 0x09, ord("H"), ord("i"), 3, 0xFF, 0xC8, 0x09])
    name, mfgrid = parse_ble_ad(payload)
    assert name == "Hi"
    assert mfgrid == 2504
    assert ble_name_match(name or "") is None
    assert mfgrid_is_flock(mfgrid or 0)


def test_flock_name_pattern() -> None:
    name = b"FS Ext Battery"
    payload = bytes([1 + len(name), 0x09, *name])
    name, _ = parse_ble_ad(payload)
    assert name and ble_name_match(name)
