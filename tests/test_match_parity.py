"""Parity between Python signatures and esp32 match.cpp label logic."""

from __future__ import annotations

from tests.conftest import MAC_DIRECT, MAC_FLOCK_CAM, MAC_MFGRID
from tests.esp32.match_parity import (
    ble_name_match,
    flock_det_confidence,
    flock_match_signal,
    mfgrid_is_flock,
    oui_is_flock_mac,
)


def test_oui_is_flock_chip() -> None:
    assert oui_is_flock_mac(MAC_FLOCK_CAM)
    assert oui_is_flock_mac(MAC_DIRECT)


def test_mfgrid_penguin() -> None:
    assert mfgrid_is_flock(2504)
    assert not mfgrid_is_flock(0)


def test_ble_name_patterns() -> None:
    assert ble_name_match("FS Ext Battery") == "fs ext battery"
    assert ble_name_match("pigvision device") == "pigvision"


def test_wifi_signals() -> None:
    assert flock_match_signal("wifi", "addr2", MAC_FLOCK_CAM) == "CHIP_OUI"
    assert flock_match_signal("wifi", "probe_request", MAC_FLOCK_CAM) == "WILDCARD_PROBE"
    assert flock_match_signal("wifi", "addr2", MAC_DIRECT) == "FLOCK_DIRECT_OUI"


def test_confidence_tiers() -> None:
    assert flock_det_confidence("wifi", "probe_request") == 3
    assert flock_det_confidence("wifi", "addr1") == 2
    assert flock_det_confidence("ble", "mfgrid") == 2
    assert flock_det_confidence("ble", "name_match", "FS Ext Battery") == 2


def test_mfgrid_mac_unused() -> None:
    _ = MAC_MFGRID
    assert flock_det_confidence("ble", "mfgrid") == 2
