"""
Core detection logic for Flock Safety device identification in WiGLE data.
No UI dependencies — import freely from tui.py or use standalone.
"""

from __future__ import annotations

import csv
import gzip
import sqlite3
from collections import Counter
from dataclasses import dataclass, field
from datetime import datetime, timezone
from math import cos, radians, sqrt
from pathlib import Path
from typing import Any, Iterator

from . import signatures as sig

# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------

CONFIDENCE: dict[int, str] = {3: "HIGH", 2: "MEDIUM", 1: "LOW"}

# Type alias for raw records yielded by readers
Record = dict[str, Any]


@dataclass
class Hit:
    mac: str
    ssid: str
    device_type: str
    lat: float
    lon: float
    rssi: int
    first_seen: str
    signals: list[tuple[str, str]] = field(default_factory=list)
    services: str = ""
    frequency: int = 0
    capabilities: str = ""
    mfgrid: int = 0

    @property
    def confidence(self) -> int:
        labels = {label for label, _ in self.signals}
        if labels & {"FLOCK_DIRECT_OUI", "RAVEN_UUID_HIGH", "FLOCKNET_SSID",
                     "FLOCK_CAMERA_SSID", "PENGUIN_BLE_SSID"}:
            return 3
        if labels & {"FLOCK_CAMERA_SSID_PATTERN", "BLE_NAME",
                     "FLOCK_WIFI_FP", "FLOCK_MFGRID"}:
            return 2
        return 1

    @property
    def confidence_label(self) -> str:
        return CONFIDENCE[self.confidence]

    def add_signal(self, label: str, detail: str = "") -> None:
        if (label, detail) not in self.signals:
            self.signals.append((label, detail))

    def signals_str(self) -> str:
        return ", ".join(f"{lbl}({det})" if det else lbl for lbl, det in self.signals)


@dataclass
class Cluster:
    """One or more nearby hits treated as a single physical installation."""

    hits: list[Hit]

    @property
    def confidence(self) -> int:
        return max(h.confidence for h in self.hits)

    @property
    def confidence_label(self) -> str:
        return CONFIDENCE[self.confidence]

    @property
    def lat(self) -> float:
        valid = [h for h in self.hits if h.lat or h.lon]
        return sum(h.lat for h in valid) / len(valid) if valid else 0.0

    @property
    def lon(self) -> float:
        valid = [h for h in self.hits if h.lat or h.lon]
        return sum(h.lon for h in valid) / len(valid) if valid else 0.0

    @property
    def label(self) -> str:
        named = [h.ssid for h in self.hits if h.ssid]
        return Counter(named).most_common(1)[0][0] if named else ""

    @property
    def types(self) -> str:
        return "/".join(sorted({h.device_type for h in self.hits}))

    @property
    def best_rssi(self) -> int:
        return max(h.rssi for h in self.hits)

    @property
    def first_seen(self) -> str:
        dates = [h.first_seen for h in self.hits if h.first_seen]
        return max(dates) if dates else ""

    @property
    def mac_list(self) -> str:
        return "\n".join(h.mac for h in self.hits)

    def enrichment_label(self, enrichment_signals: frozenset[str]) -> str:
        """Compact string of enrichment signals present across all hits in the cluster."""
        icons = {
            "OSM_ALPR_NEARBY":  "🗺",
            "ALPRWATCH_NEARBY": "📍",
            "WIGLE_SEEN":       "📡",
        }
        found: list[str] = []
        seen: set[str] = set()
        for h in self.hits:
            for label, _ in h.signals:
                if label in enrichment_signals and label not in seen:
                    seen.add(label)
                    found.append(icons.get(label, label))
        return " ".join(found) if found else "—"

    @property
    def maps_url(self) -> str:
        return f"https://maps.google.com/?q={self.lat},{self.lon}"

    @property
    def streetview_url(self) -> str:
        return f"https://maps.google.com/?layer=c&cbll={self.lat},{self.lon}"


# ---------------------------------------------------------------------------
# Clustering
# ---------------------------------------------------------------------------

def _dist_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    dlat = (lat2 - lat1) * 111_320
    dlon = (lon2 - lon1) * 111_320 * cos(radians((lat1 + lat2) / 2))
    return sqrt(dlat**2 + dlon**2)


def cluster_hits(hits: list[Hit], radius_m: float = 75.0) -> list[Cluster]:
    """Group hits within radius_m metres of each other into Clusters."""
    parent = list(range(len(hits)))

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i in range(len(hits)):
        for j in range(i + 1, len(hits)):
            hi, hj = hits[i], hits[j]
            if (hi.lat or hi.lon) and (hj.lat or hj.lon):
                if _dist_m(hi.lat, hi.lon, hj.lat, hj.lon) <= radius_m:
                    parent[find(i)] = find(j)

    groups: dict[int, list[Hit]] = {}
    for i, h in enumerate(hits):
        groups.setdefault(find(i), []).append(h)

    return sorted(
        [Cluster(g) for g in groups.values()],
        key=lambda c: (-c.confidence, c.label),
    )


def single_clusters(hits: list[Hit]) -> list[Cluster]:
    """Wrap each hit in its own Cluster (ungrouped display mode)."""
    return [Cluster([h]) for h in hits]


# ---------------------------------------------------------------------------
# Detection
# ---------------------------------------------------------------------------

def _oui(mac: str) -> str:
    return mac.lower()[:8]


def analyze(  # noqa: PLR0912 (many branches by design)
    mac: str,
    ssid: str,
    type: str = "",           # noqa: A002 — matches WiGLE field name
    device_type: str = "",
    lat: float = 0.0,
    lon: float = 0.0,
    rssi: int = 0,
    first_seen: str = "",
    services: str = "",
    frequency: int = 0,
    capabilities: str = "",
    mfgrid: int = 0,
) -> Hit | None:
    device_type = device_type or type
    mac_l = mac.lower()
    oui = _oui(mac_l)
    ssid_l = (ssid or "").lower().strip()
    svc_set = set((services or "").lower().split())
    is_wifi = device_type in ("W", "WIFI")
    is_ble  = device_type in ("B", "E", "BLE", "BT")
    hit: Hit | None = None

    def h() -> Hit:
        nonlocal hit
        if hit is None:
            hit = Hit(
                mac=mac, ssid=ssid or "", device_type=device_type,
                lat=lat, lon=lon, rssi=rssi, first_seen=first_seen,
                services=services or "", frequency=frequency,
                capabilities=capabilities or "", mfgrid=mfgrid,
            )
        return hit

    if oui in sig.FLOCK_DIRECT_OUIS:
        h().add_signal("FLOCK_DIRECT_OUI", oui)

    if oui in sig.FLOCK_CHIP_OUIS:
        h().add_signal("CHIP_OUI", oui)

    if oui in sig.FLOCK_BACKHAUL_OUIS:
        if "flock" in ssid_l:
            h().add_signal("FLOCKNET_SSID", ssid)
        elif ssid_l == "":
            h().add_signal("BACKHAUL_OUI_HIDDEN", oui)

    if is_wifi:
        if sig.FLOCK_CAMERA_SSID_RE.match(ssid or ""):
            mac_suffix  = mac.replace(":", "")[-6:].upper()
            ssid_suffix = (ssid or "").split("-")[-1].upper()
            if mac_suffix and mac_suffix == ssid_suffix:
                h().add_signal("FLOCK_CAMERA_SSID", ssid)
            elif oui in sig.FLOCK_CHIP_OUIS:
                h().add_signal("FLOCK_CAMERA_SSID", f"{ssid} (OUI corroborated)")
            else:
                h().add_signal("FLOCK_CAMERA_SSID_PATTERN", ssid)
        elif ssid_l == "flocknet":
            h().add_signal("FLOCKNET_SSID", ssid)
        else:
            for pat in sig.FLOCK_SSID_PATTERNS:
                if pat in ssid_l:
                    h().add_signal("SSID_PATTERN", pat)
                    break

        if (
            oui in sig.FLOCK_CHIP_OUIS
            and capabilities == sig.FLOCK_WIFI_CAPAB
            and frequency in sig.FLOCK_CAMERA_CHANNELS_MHZ
        ):
            h().add_signal("FLOCK_WIFI_FP", f"OUI+WPA2+ch{frequency}MHz")

    if is_ble:
        if sig.PENGUIN_BLE_SSID_RE.match(ssid or ""):
            h().add_signal("PENGUIN_BLE_SSID", ssid)
        else:
            for pat in sig.BLE_NAME_PATTERNS:
                if pat in ssid_l:
                    h().add_signal("BLE_NAME", pat)
                    break

        if mfgrid and mfgrid in sig.FLOCK_MFGRIDS:
            h().add_signal("FLOCK_MFGRID", f"mfgrid={mfgrid}")

    matched_high = svc_set & sig.RAVEN_SERVICES_HIGH
    if matched_high:
        h().add_signal("RAVEN_UUID_HIGH", ", ".join(sorted(matched_high)))

    if not is_wifi:
        meaningful = (svc_set & sig.RAVEN_SERVICES_OLD) - {
            "0000180a-0000-1000-8000-00805f9b34fb"
        }
        if meaningful:
            h().add_signal("RAVEN_UUID_OLD", ", ".join(sorted(meaningful)))

    if oui in sig.SURVEILLANCE_OUIS:
        h().add_signal("SURVEILLANCE_OUI", sig.SURVEILLANCE_OUIS[oui])

    return hit


# ---------------------------------------------------------------------------
# Readers
# ---------------------------------------------------------------------------

def _parse_wigle_time(raw) -> str:
    """Convert WiGLE's lasttime (Unix ms or s integer) to 'YYYY-MM-DD HH:MM:SS' UTC."""
    if not raw:
        return ""
    try:
        ts = int(raw)
        if ts > 1e10:  # milliseconds → seconds
            ts //= 1000
        return datetime.fromtimestamp(ts, tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    except (ValueError, OSError):
        return str(raw)


def read_sqlite(path: Path) -> Iterator[Record]:
    conn = sqlite3.connect(str(path))
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()
    cur.execute(
        "SELECT bssid, ssid, type, bestlevel, bestlat, bestlon, "
        "lasttime, service, frequency, capabilities, mfgrid FROM network"
    )
    try:
        for row in cur.fetchall():
            yield {
                "mac":          row["bssid"] or "",
                "ssid":         row["ssid"] or "",
                "type":         row["type"] or "",
                "rssi":         row["bestlevel"] or 0,
                "lat":          float(row["bestlat"] or 0),
                "lon":          float(row["bestlon"] or 0),
                "first_seen":   _parse_wigle_time(row["lasttime"]),
                "services":     row["service"] or "",
                "frequency":    row["frequency"] or 0,
                "capabilities": row["capabilities"] or "",
                "mfgrid":       int(row["mfgrid"] or 0),
            }
    finally:
        conn.close()


def read_csv(path: Path) -> Iterator[Record]:
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(str(path), "rt", encoding="utf-8", errors="replace") as f:
        first = f.readline()
        if not first.startswith("WigleWifi"):
            f.seek(0)
        reader = csv.DictReader(f)
        for row in reader:
            yield {
                "mac":          row.get("MAC", ""),
                "ssid":         row.get("SSID", ""),
                "type":         row.get("Type", ""),
                "rssi":         int(row.get("RSSI", 0) or 0),
                "lat":          float(row.get("CurrentLatitude", 0) or 0),
                "lon":          float(row.get("CurrentLongitude", 0) or 0),
                "first_seen":   row.get("FirstSeen", ""),
                "services":     "",
                "frequency":    int(row.get("Frequency", 0) or 0),
                "capabilities": row.get("AuthMode", ""),
                "mfgrid":       0,
            }


def load_records(path: Path) -> Iterator[Record]:
    suffix = path.suffix.lower()
    if suffix in (".sqlite", ".sqlite3", ".db"):
        yield from read_sqlite(path)
    elif suffix in (".gz", ".csv"):
        yield from read_csv(path)
    else:
        raise ValueError(f"Unsupported file type: {suffix}")


def run_detection(path: Path, min_confidence: int = 1) -> tuple[list[Hit], int]:
    """Return (hits sorted by confidence desc, total records scanned)."""
    seen: dict[str, Hit] = {}
    total = 0
    for rec in load_records(path):
        total += 1
        h = analyze(**rec)
        if h and h.confidence >= min_confidence:
            mac_key = rec["mac"].lower()
            if mac_key not in seen:
                seen[mac_key] = h
            else:
                for s in h.signals:
                    seen[mac_key].add_signal(*s)
    return sorted(seen.values(), key=lambda h: (-h.confidence, h.mac)), total


# ---------------------------------------------------------------------------
# Export helpers
# ---------------------------------------------------------------------------

def export_csv(hits: list[Hit], path: str) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow([
            "confidence", "type", "mac", "ssid", "lat", "lon",
            "rssi", "freq_mhz", "capabilities", "mfgrid",
            "signals", "services", "first_seen",
        ])
        for h in hits:
            w.writerow([
                h.confidence_label, h.device_type, h.mac, h.ssid,
                h.lat, h.lon, h.rssi, h.frequency, h.capabilities,
                h.mfgrid, h.signals_str(), h.services, h.first_seen,
            ])


def export_geojson(hits: list[Hit], path: str) -> None:
    """Export hits as a GeoJSON FeatureCollection (EPSG:4326)."""
    import json as _json
    features = []
    for h in hits:
        if h.lat == 0.0 and h.lon == 0.0:
            continue
        features.append({
            "type": "Feature",
            "geometry": {"type": "Point", "coordinates": [h.lon, h.lat]},
            "properties": {
                "mac":          h.mac,
                "ssid":         h.ssid,
                "device_type":  h.device_type,
                "confidence":   h.confidence_label,
                "confidence_n": h.confidence,
                "rssi":         h.rssi,
                "frequency_mhz": h.frequency,
                "first_seen":   h.first_seen,
                "signals":      h.signals_str(),
                "capabilities": h.capabilities,
                "mfgrid":       h.mfgrid,
            },
        })
    with open(path, "w", encoding="utf-8") as f:
        _json.dump({"type": "FeatureCollection", "features": features}, f, indent=2)


def export_kml(hits: list[Hit], path: str) -> None:
    colors = {3: "ff0000ff", 2: "ff00aaff", 1: "ff00ffff"}
    icons  = {3: "1",        2: "2",        1: "3"}
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<kml xmlns="http://www.opengis.net/kml/2.2">',
        "<Document>",
        "<name>Flock Safety Device Detections</name>",
    ]
    for level in (3, 2, 1):
        lines.append(
            f'<Style id="conf{level}"><IconStyle><color>{colors[level]}</color>'
            f'<Icon><href>http://maps.google.com/mapfiles/kml/paddle/{icons[level]}.png'
            f"</href></Icon></IconStyle></Style>"
        )
    for h in hits:
        if h.lat == 0.0 and h.lon == 0.0:
            continue
        desc = (
            f"MAC: {h.mac}\nType: {h.device_type}\nRSSI: {h.rssi} dBm\n"
            f"Freq: {h.frequency} MHz\nSignals: {h.signals_str()}\n"
            f"Seen: {h.first_seen}"
        )
        lines += [
            "<Placemark>",
            f"<name>{h.ssid or h.mac}</name>",
            f"<description><![CDATA[{desc}]]></description>",
            f"<styleUrl>#conf{h.confidence}</styleUrl>",
            f"<Point><coordinates>{h.lon},{h.lat},0</coordinates></Point>",
            "</Placemark>",
        ]
    lines += ["</Document>", "</kml>"]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
