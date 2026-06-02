"""Mirror of esp32/src/match.cpp using signatures.py (Python source of truth)."""

from __future__ import annotations

from flockdar import signatures as sig

_FLOCK_OUIS = sorted(sig.FLOCK_DIRECT_OUIS | sig.FLOCK_CHIP_OUIS)
_FLOCK_MFGRIDS = sorted(sig.FLOCK_MFGRIDS)
_BLE_PATTERNS = [p.lower() for p in sig.BLE_NAME_PATTERNS]
_DIRECT = {o.lower() for o in sig.FLOCK_DIRECT_OUIS}


def _mac_bytes(mac: str) -> tuple[int, int, int]:
    parts = mac.split(":")
    return int(parts[0], 16), int(parts[1], 16), int(parts[2], 16)


def oui_is_flock_mac(mac: str) -> bool:
    prefix = ":".join(mac.split(":")[:3]).lower()
    return prefix in {o.lower() for o in _FLOCK_OUIS}


def oui_is_flock(mac: bytes) -> bool:
    prefix = f"{mac[0]:02x}:{mac[1]:02x}:{mac[2]:02x}"
    return oui_is_flock_mac(prefix + ":00:00:00")


def mfgrid_is_flock(cid: int) -> bool:
    return cid in _FLOCK_MFGRIDS


def ble_name_match(name: str) -> str | None:
    low = name.lower()
    for pat in _BLE_PATTERNS:
        if pat in low:
            return pat
    return None


def flock_match_signal(kind: str, method: str, mac: str) -> str:
    b0, b1, b2 = _mac_bytes(mac)
    oui = f"{b0:02x}:{b1:02x}:{b2:02x}"
    direct = oui in _DIRECT
    if kind == "wifi":
        if method == "probe_request":
            return "WILDCARD_PROBE"
        return "FLOCK_DIRECT_OUI" if direct else "CHIP_OUI"
    if kind == "ble" and method == "name_match":
        return "BLE_NAME"
    if kind == "ble" and method == "mfgrid":
        return "FLOCK_MFGRID"
    return "FLOCK_MATCH"


def flock_det_confidence(kind: str, method: str, name: str | None = None) -> int:
    if method == "probe_request":
        return 3
    if method == "addr2":
        return 3
    if method == "addr1":
        return 2
    if method == "mfgrid":
        return 2
    if method == "name_match" and name:
        pat = ble_name_match(name)
        if pat and "enguin" in pat:
            return 3
        return 2
    if kind == "ble":
        return 2
    return 1
