"""
Enrichment module — cross-references detected hits against external databases.

Three async enrichers, all optional and independently enable-able:

  OverpassEnricher   — queries DeFlock's Overpass mirror for OSM ALPR nodes
                       near each hit. Free, no auth required.

  ALPRWatchEnricher  — downloads alprwatch.org's daily KMZ export of confirmed
                       ALPR locations and does a local spatial lookup. Free,
                       no auth required. Caches the KMZ for 24 h.

  WiGLEEnricher      — looks up each MAC in the WiGLE API. Requires a WiGLE
                       API key (free account at wigle.net). Credentials are
                       read from env vars WIGLE_API_NAME / WIGLE_API_TOKEN or
                       from ~/.config/flock-wigle/config.json (chmod 600).

Usage:
    import asyncio
    from enrich import build_enrichers, enrich_hits_async

    enrichers = build_enrichers()
    asyncio.run(enrich_hits_async(hits, enrichers, callback=lambda h: ...))
"""

from __future__ import annotations

import asyncio
import base64
import json
import os
import sys
import time
import xml.etree.ElementTree as ET
import zipfile
from abc import ABC, abstractmethod
from collections import OrderedDict
from math import cos, radians, sqrt
from pathlib import Path
from typing import Any, Callable

import httpx

from .detect import Hit

# ---------------------------------------------------------------------------
# Types
# ---------------------------------------------------------------------------

Signal = tuple[str, str]
_Camera = tuple[float, float, str]   # (lat, lon, name)

# Signal labels produced by enrichers (used by TUI to display enrichment column)
ENRICHMENT_SIGNAL_LABELS: frozenset[str] = frozenset({
    "OSM_ALPR_NEARBY",
    "ALPRWATCH_NEARBY",
    "WIGLE_SEEN",
})

_UA = "flock-wigle-detect/1.0"

# Per-enricher cache TTLs (seconds)
_ENRICHER_TTLS: dict[str, float] = {
    "OSM/DeFlock": 7 * 86_400,   # OSM changes slowly
    "ALPRWatch":   86_400,        # matches KMZ refresh cadence
    "WiGLE":       7 * 86_400,
}

# ---------------------------------------------------------------------------
# Platform paths
# ---------------------------------------------------------------------------

def _cache_dir() -> Path:
    if sys.platform == "win32":
        base = Path(os.environ.get("LOCALAPPDATA") or "") or Path.home()
    else:
        base = Path(os.environ.get("XDG_CACHE_HOME") or "") or Path.home() / ".cache"
    d = base / "flock-wigle"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _config_path() -> Path:
    if sys.platform == "win32":
        base = Path(os.environ.get("APPDATA") or "") or Path.home()
    else:
        base = Path(os.environ.get("XDG_CONFIG_HOME") or "") or Path.home() / ".config"
    d = base / "flock-wigle"
    d.mkdir(parents=True, exist_ok=True)
    return d / "config.json"


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

def load_config() -> dict[str, str]:
    p = _config_path()
    if not p.exists():
        return {}
    if sys.platform != "win32" and (p.stat().st_mode & 0o077):
        p.chmod(0o600)
    try:
        raw = json.loads(p.read_text(encoding="utf-8"))
        return {k: str(v) for k, v in raw.items() if isinstance(v, (str, int, float))}
    except (json.JSONDecodeError, OSError):
        return {}


def save_config(cfg: dict[str, str]) -> None:
    p = _config_path()
    p.write_text(json.dumps(cfg, indent=2), encoding="utf-8")
    if sys.platform != "win32":
        p.chmod(0o600)


# ---------------------------------------------------------------------------
# Bounded LRU cache
# ---------------------------------------------------------------------------

class _EnrichCache:
    """MAC-keyed disk cache for enrichment results, keyed by enricher name.

    Format: { mac: { enricher_name: { "signals": [[lbl, det], …], "ts": float } } }
    A stored empty signals list means "was queried, nothing found" — still cached.
    """

    def __init__(self, path: Path) -> None:
        self._path = path
        self._data: dict[str, dict[str, Any]] = {}
        self._dirty = False
        if path.exists():
            try:
                self._data = json.loads(path.read_text(encoding="utf-8"))
            except (json.JSONDecodeError, OSError):
                self._data = {}

    def get(self, mac: str, enricher_name: str, ttl_s: float) -> list[Signal] | None:
        """Return cached signals or None if absent/expired."""
        entry = self._data.get(mac.lower(), {}).get(enricher_name)
        if entry is None:
            return None
        if time.time() - entry.get("ts", 0) > ttl_s:
            return None
        return [(s[0], s[1]) for s in entry.get("signals", [])]

    def set(self, mac: str, enricher_name: str, signals: list[Signal]) -> None:
        mac_key = mac.lower()
        if mac_key not in self._data:
            self._data[mac_key] = {}
        self._data[mac_key][enricher_name] = {
            "signals": [[lbl, det] for lbl, det in signals],
            "ts": time.time(),
        }
        self._dirty = True

    def save(self) -> None:
        if not self._dirty:
            return
        try:
            self._path.write_text(json.dumps(self._data), encoding="utf-8")
            self._dirty = False
        except OSError:
            pass


def load_enrich_cache() -> "_EnrichCache":
    return _EnrichCache(_cache_dir() / "enrichment-cache.json")


def apply_cached_enrichment(hits: list[Hit], cache: "_EnrichCache") -> None:
    """Apply non-expired cached enrichment signals to hits — no network calls."""
    for hit in hits:
        if not hit.mac:
            continue
        for ename, ttl in _ENRICHER_TTLS.items():
            cached = cache.get(hit.mac, ename, ttl)
            if cached:
                for label, detail in cached:
                    hit.add_signal(label, detail)


class _BoundedCache(OrderedDict[Any, Any]):
    """OrderedDict that evicts the oldest entry when maxsize is exceeded."""

    def __init__(self, maxsize: int = 512) -> None:
        super().__init__()
        self._maxsize = maxsize

    def __setitem__(self, key: Any, value: Any) -> None:
        super().__setitem__(key, value)
        if len(self) > self._maxsize:
            self.popitem(last=False)


# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------

def _dist_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    dlat = (lat2 - lat1) * 111_320
    dlon = (lon2 - lon1) * 111_320 * cos(radians((lat1 + lat2) / 2))
    return sqrt(dlat**2 + dlon**2)


def _make_client(transport: httpx.AsyncBaseTransport | None = None) -> httpx.AsyncClient:
    """Create an httpx client; inject a transport for testing."""
    kwargs: dict[str, Any] = {
        "headers": {"User-Agent": _UA},
        "timeout": httpx.Timeout(20.0),
        "follow_redirects": True,
    }
    if transport is not None:
        kwargs["transport"] = transport
    return httpx.AsyncClient(**kwargs)


# ---------------------------------------------------------------------------
# Base class
# ---------------------------------------------------------------------------

class Enricher(ABC):
    name: str

    @abstractmethod
    async def enrich(self, hit: Hit) -> list[Signal]:
        """Return list of (signal_label, detail) tuples to add to hit."""

    def ready(self) -> bool:
        return True


# ---------------------------------------------------------------------------
# Overpass (DeFlock OSM) enricher
# ---------------------------------------------------------------------------

_OVERPASS_PRIMARY  = "https://overpass.deflock.org/api/interpreter"
_OVERPASS_FALLBACK = "https://overpass-api.de/api/interpreter"
_ALPR_QUERY_TMPL = (
    "[out:json][timeout:15];\n"
    "(\n"
    '  node(around:{r},{lat},{lon})["man_made"="surveillance"]["surveillance:type"="ALPR"];\n'
    '  node(around:{r},{lat},{lon})["man_made"="surveillance"]["surveillance:type"="alpr"];\n'
    '  node(around:{r},{lat},{lon})["camera:type"="ALPR"];\n'
    ");\n"
    "out tags center;"
)


class OverpassEnricher(Enricher):
    name = "OSM/DeFlock"

    def __init__(
        self,
        radius_m: int = 150,
        *,
        transport: httpx.AsyncBaseTransport | None = None,
        cache: "_EnrichCache | None" = None,
    ) -> None:
        self._radius = radius_m
        self._transport = transport
        self._mem_cache: _BoundedCache = _BoundedCache(maxsize=512)
        self._disk_cache = cache

    async def enrich(self, hit: Hit) -> list[Signal]:
        if not (hit.lat or hit.lon):
            return []
        if self._disk_cache is not None:
            cached = self._disk_cache.get(hit.mac, self.name, _ENRICHER_TTLS[self.name])
            if cached is not None:
                return cached
        key: tuple[float, float] = (round(hit.lat, 4), round(hit.lon, 4))
        if key not in self._mem_cache:
            self._mem_cache[key] = await self._query(hit.lat, hit.lon)
        nodes: list[dict[str, Any]] = self._mem_cache[key]
        if not nodes:
            result: list[Signal] = []
        else:
            closest = min(nodes, key=lambda n: _dist_m(hit.lat, hit.lon, n["lat"], n["lon"]))
            dist = _dist_m(hit.lat, hit.lon, closest["lat"], closest["lon"])
            tags: dict[str, str] = closest.get("tags", {})
            label = tags.get("name") or tags.get("ref") or f"OSM:{closest['id']}"
            operator = tags.get("operator", "")
            detail = f"{label} ({operator}, {dist:.0f}m)" if operator else f"{label} {dist:.0f}m"
            result = [("OSM_ALPR_NEARBY", detail)]
        if self._disk_cache is not None:
            self._disk_cache.set(hit.mac, self.name, result)
        return result

    async def _query(self, lat: float, lon: float) -> list[dict[str, Any]]:
        query = _ALPR_QUERY_TMPL.format(r=self._radius, lat=lat, lon=lon)
        async with _make_client(self._transport) as client:
            for endpoint in (_OVERPASS_PRIMARY, _OVERPASS_FALLBACK):
                try:
                    resp = await client.post(endpoint, data={"data": query})
                    resp.raise_for_status()
                    return resp.json().get("elements", [])
                except Exception:  # noqa: BLE001
                    continue
        return []


# ---------------------------------------------------------------------------
# ALPRWatch KMZ enricher
# ---------------------------------------------------------------------------

_ALPRWATCH_URL   = "https://alprwatch.org/pub/avoidance/alprwatch-avoidance-alpr-latest.kmz"
_KMZ_CACHE_TTL_S = 86_400  # 24 hours
_KML_NS          = "{http://www.opengis.net/kml/2.2}"


class ALPRWatchEnricher(Enricher):
    name = "ALPRWatch"

    def __init__(
        self,
        radius_m: float = 150.0,
        *,
        transport: httpx.AsyncBaseTransport | None = None,
        cache: "_EnrichCache | None" = None,
    ) -> None:
        self._radius = radius_m
        self._transport = transport
        self._cameras: list[_Camera] = []
        self._lock = asyncio.Lock()
        self._loaded = False
        self._disk_cache = cache

    def ready(self) -> bool:
        return self._loaded and bool(self._cameras)

    async def enrich(self, hit: Hit) -> list[Signal]:
        if not (hit.lat or hit.lon):
            return []
        if self._disk_cache is not None:
            cached = self._disk_cache.get(hit.mac, self.name, _ENRICHER_TTLS[self.name])
            if cached is not None:
                return cached
        await self._ensure_loaded()
        cameras = self._cameras  # local ref; list replaced atomically on reload

        if not cameras:
            return []

        lat_margin = self._radius / 111_320
        best_dist = float("inf")
        best_name = ""
        for clat, clon, cname in cameras:
            if abs(clat - hit.lat) > lat_margin:
                continue
            d = _dist_m(hit.lat, hit.lon, clat, clon)
            if d < best_dist:
                best_dist, best_name = d, cname

        result: list[Signal] = (
            [("ALPRWATCH_NEARBY", f"{best_name} {best_dist:.0f}m")]
            if best_dist <= self._radius else []
        )
        if self._disk_cache is not None:
            self._disk_cache.set(hit.mac, self.name, result)
        return result

    async def _ensure_loaded(self) -> None:
        async with self._lock:
            if self._loaded:
                return
            self._loaded = True
            kmz_path = _cache_dir() / "alprwatch-latest.kmz"
            stale = (
                not kmz_path.exists()
                or (time.time() - kmz_path.stat().st_mtime) > _KMZ_CACHE_TTL_S
            )
            if stale:
                await self._download(kmz_path)
            if kmz_path.exists():
                # Parse in a thread — ET.iterparse is CPU-bound
                cameras = await asyncio.to_thread(self._parse_kmz, kmz_path)
                self._cameras = cameras

    async def _download(self, kmz_path: Path) -> None:
        try:
            async with _make_client(self._transport) as client:
                resp = await client.get(_ALPRWATCH_URL)
                resp.raise_for_status()
                kmz_path.write_bytes(resp.content)
        except Exception:  # noqa: BLE001
            pass

    @staticmethod
    def _parse_kmz(kmz_path: Path) -> list[_Camera]:
        """Streaming KML parse; never loads full tree into memory."""
        cameras: list[_Camera] = []
        try:
            with zipfile.ZipFile(kmz_path) as zf:
                kml_name = next((n for n in zf.namelist() if n.endswith(".kml")), None)
                if not kml_name:
                    return cameras
                with zf.open(kml_name) as kml_file:
                    in_pm = False
                    pm_name = ""
                    coords_text = ""
                    for event, elem in ET.iterparse(kml_file, events=("start", "end")):
                        tag = elem.tag
                        if event == "start":
                            if tag == f"{_KML_NS}Placemark":
                                in_pm, pm_name, coords_text = True, "", ""
                        elif event == "end":
                            if not in_pm:
                                elem.clear()
                                continue
                            if tag == f"{_KML_NS}name":
                                pm_name = (elem.text or "").strip()
                            elif tag == f"{_KML_NS}coordinates":
                                coords_text = (elem.text or "").strip()
                            elif tag == f"{_KML_NS}Placemark":
                                in_pm = False
                                if coords_text:
                                    try:
                                        parts = coords_text.split(",")
                                        cameras.append((float(parts[1]), float(parts[0]), pm_name))
                                    except (ValueError, IndexError):
                                        pass
                            elem.clear()
        except Exception:  # noqa: BLE001
            pass
        return cameras


# ---------------------------------------------------------------------------
# WiGLE API enricher
# ---------------------------------------------------------------------------

_WIGLE_BASE    = "https://api.wigle.net/api/v2"
_WIGLE_RATELIM = 6.5  # seconds between requests (free tier ≈ 10/min)


class WiGLEEnricher(Enricher):
    name = "WiGLE"

    def __init__(
        self,
        api_name: str,
        api_token: str,
        *,
        transport: httpx.AsyncBaseTransport | None = None,
        cache: "_EnrichCache | None" = None,
    ) -> None:
        cred = base64.b64encode(f"{api_name}:{api_token}".encode()).decode()
        self.__auth_header = f"Basic {cred}"  # name-mangled; not externally readable
        self._transport = transport
        self._lock = asyncio.Lock()
        self._last_req = 0.0
        self._disk_cache = cache

    async def enrich(self, hit: Hit) -> list[Signal]:
        if not hit.mac:
            return []
        if self._disk_cache is not None:
            cached = self._disk_cache.get(hit.mac, self.name, _ENRICHER_TTLS[self.name])
            if cached is not None:
                return cached
        # Serialise all WiGLE calls and enforce rate limit
        async with self._lock:
            gap = _WIGLE_RATELIM - (time.monotonic() - self._last_req)
            if gap > 0:
                await asyncio.sleep(gap)
            self._last_req = time.monotonic()
            result = await self._fetch(hit.mac)
        if self._disk_cache is not None:
            self._disk_cache.set(hit.mac, self.name, result)
        return result

    async def _fetch(self, mac: str) -> list[Signal]:
        url = f"{_WIGLE_BASE}/network/detail"
        try:
            async with _make_client(self._transport) as client:
                resp = await client.get(
                    url,
                    params={"netid": mac},
                    headers={"Authorization": self.__auth_header, "Accept": "application/json"},
                )
            if resp.status_code == 404:
                return [("WIGLE_NOT_FOUND", "not in WiGLE DB")]
            resp.raise_for_status()
            data: dict[str, Any] = resp.json()
        except httpx.HTTPStatusError:
            return []
        except Exception:  # noqa: BLE001
            return []

        results: list[dict[str, Any]] = data.get("results", [])
        if not results:
            return [("WIGLE_NOT_FOUND", "no results")]
        net = results[0]
        loc: list[dict[str, Any]] = net.get("locationData") or []
        count: Any = loc[0].get("total", "?") if loc else "?"
        first = (net.get("firsttime") or "?")[:10]
        last  = (net.get("lasttime")  or "?")[:10]
        detail = f"first={first} last={last} count={count}"
        trilat, trilong = net.get("trilat", ""), net.get("trilong", "")
        if trilat and trilong:
            detail += f" loc={trilat},{trilong}"
        return [("WIGLE_SEEN", detail)]


# ---------------------------------------------------------------------------
# Top-level API
# ---------------------------------------------------------------------------

def build_enrichers(
    wigle_name: str = "",
    wigle_token: str = "",
    overpass: bool = True,
    alprwatch: bool = True,
    cache: "_EnrichCache | None" = None,
) -> list[Enricher]:
    """Assemble enabled enrichers from args, env vars, and config file."""
    enrichers: list[Enricher] = []
    if overpass:
        enrichers.append(OverpassEnricher(cache=cache))
    if alprwatch:
        enrichers.append(ALPRWatchEnricher(cache=cache))

    name  = wigle_name  or os.environ.get("WIGLE_API_NAME",  "")
    token = wigle_token or os.environ.get("WIGLE_API_TOKEN", "")
    if not (name and token):
        cfg = load_config()
        name  = name  or cfg.get("wigle_api_name",  "")
        token = token or cfg.get("wigle_api_token", "")
    if name and token:
        enrichers.append(WiGLEEnricher(name, token, cache=cache))

    return enrichers


async def enrich_hits_async(
    hits: list[Hit],
    enrichers: list[Enricher],
    callback: Callable[[Hit], None] | None = None,
    cache: "_EnrichCache | None" = None,
) -> None:
    """
    Enrich all hits concurrently.

    Overpass/ALPRWatch queries for every hit run in parallel.
    WiGLE calls are serialised internally via asyncio.Lock + rate limiting.
    Calls callback(hit) after each hit finishes so the UI can update live.
    """
    async def _process(hit: Hit) -> None:
        if hit.lat or hit.lon:
            results = await asyncio.gather(
                *[e.enrich(hit) for e in enrichers],
                return_exceptions=True,
            )
            for r in results:
                if isinstance(r, list):
                    for label, detail in r:
                        hit.add_signal(label, detail)
        if callback:
            callback(hit)

    await asyncio.gather(*[_process(h) for h in hits])
    if cache is not None:
        await asyncio.to_thread(cache.save)


def enrich_hits(
    hits: list[Hit],
    enrichers: list[Enricher],
    callback: Callable[[Hit], None] | None = None,
) -> None:
    """Sync wrapper around enrich_hits_async for scripted / non-async use."""
    asyncio.run(enrich_hits_async(hits, enrichers, callback))
