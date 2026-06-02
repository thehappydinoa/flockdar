"""Mirror of esp32/src/wifi_scanner.cpp promiscuous management-frame rules."""

from __future__ import annotations

from tests.esp32.match_parity import oui_is_flock

SUBTYPE_PROBE_REQ = 0x04


def parse_mgmt_frame(payload: bytes) -> list[tuple[str, bytes, int, int]]:
    """Return list of (method, mac, rssi, channel) Flock enqueue candidates."""
    if len(payload) < 24:
        return []
    fc0 = payload[0]
    ftype = (fc0 >> 2) & 0x3
    fsub = (fc0 >> 4) & 0xF
    if ftype != 0:
        return []
    addr1 = payload[4:10]
    addr2 = payload[10:16]
    rssi = 0
    channel = 0
    out: list[tuple[str, bytes, int, int]] = []

    if oui_is_flock(addr2):
        out.append(("addr2", addr2, rssi, channel))
    if not (addr1[0] & 0x01) and not (addr1[0] & 0x02) and oui_is_flock(addr1):
        out.append(("addr1", addr1, rssi, channel))
    if (
        fsub == SUBTYPE_PROBE_REQ
        and len(payload) >= 26
        and oui_is_flock(addr2)
        and payload[24] == 0
        and payload[25] == 0
    ):
        out.append(("probe_request", addr2, rssi, channel))
    return out


def should_note_wifi(addr2: bytes) -> bool:
    return (addr2[0] & 0x03) == 0
