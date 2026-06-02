"""Mirror of esp32/src/ble_scanner.cpp AD structure walker."""

from __future__ import annotations

from collections.abc import Callable


def ad_walk(
    payload: bytes,
    fn: Callable[[int, bytes], bool],
) -> bool:
    off = 0
    while off + 1 < len(payload):
        field_len = payload[off]
        if field_len == 0 or off + field_len >= len(payload):
            break
        ad_type = payload[off + 1]
        data = payload[off + 2 : off + 1 + field_len]
        if fn(ad_type, data):
            return True
        off += field_len + 1
    return False


def parse_ble_ad(payload: bytes) -> tuple[str | None, int | None]:
    name: str | None = None
    mfgrid: int | None = None

    def visitor(ad_type: int, data: bytes) -> bool:
        nonlocal name, mfgrid
        if ad_type in (0x08, 0x09) and data and name is None:
            name = data.decode("utf-8", errors="replace")
            return False
        if ad_type == 0xFF and len(data) >= 2:
            mfgrid = data[0] | (data[1] << 8)
            return False
        return False

    ad_walk(payload, visitor)
    return name, mfgrid
