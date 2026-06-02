"""WiFi management-frame parsing rules (mirrors wifi_scanner.cpp)."""

from __future__ import annotations

from tests.conftest import MAC_FLOCK_CAM
from tests.esp32.wifi_frame import parse_mgmt_frame, should_note_wifi


def _mgmt_frame(
    addr1: bytes,
    addr2: bytes,
    *,
    ftype: int = 0,
    fsub: int = 0,
    ssid_tag_id: int = 0,
    ssid_tag_len: int = 0,
) -> bytes:
    pl = bytearray(26)
    pl[0] = (fsub << 4) | (ftype << 2)
    pl[4:10] = addr1
    pl[10:16] = addr2
    pl[24] = ssid_tag_id
    pl[25] = ssid_tag_len
    return bytes(pl)


def test_rejects_short_and_non_mgmt() -> None:
    assert parse_mgmt_frame(b"\x00" * 10) == []
    pl = bytearray(26)
    pl[0] = 1 << 2  # data frame
    assert parse_mgmt_frame(bytes(pl)) == []


def test_addr2_flock_oui() -> None:
    mac = bytes.fromhex(MAC_FLOCK_CAM.replace(":", ""))
    hits = parse_mgmt_frame(_mgmt_frame(b"\xff" * 6, mac))
    methods = {h[0] for h in hits}
    assert "addr2" in methods


def test_probe_request_wildcard() -> None:
    mac = bytes.fromhex(MAC_FLOCK_CAM.replace(":", ""))
    hits = parse_mgmt_frame(
        _mgmt_frame(b"\xff" * 6, mac, fsub=0x04, ssid_tag_id=0, ssid_tag_len=0)
    )
    assert "probe_request" in {h[0] for h in hits}


def test_multicast_skipped_for_nearby() -> None:
    mcast = bytes([0x01, 0, 0, 0, 0, 0])
    assert not should_note_wifi(mcast)
    unicast = bytes.fromhex(MAC_FLOCK_CAM.replace(":", ""))
    assert should_note_wifi(unicast)
