"""
WiGLE-based Flock device discovery.

Searches WiGLE's crowdsourced database for Flock Safety devices that the user
has never personally driven past. Results are cached to disk (24 h TTL) so
API calls are not wasted on repeated runs.

Requires a WiGLE API key (free account at wigle.net). Credentials are read
from the same config as enrich.py: env vars WIGLE_API_NAME / WIGLE_API_TOKEN
or ~/.config/flock-wigle/config.json.
"""

from __future__ import annotations

import asyncio
import base64
import json
import os
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import httpx

from . import signatures as sig
from .detect import Hit, analyze
from .enrich import _cache_dir, _make_client, load_config

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

_WIGLE_BASE = "https://api.wigle.net/api/v2"

# Drop WiFi results last seen before this year — filters old home/business
# networks that happen to share SSID patterns ("Flocknet" from 2009, etc.).
# Flock Safety was founded in 2017, so anything older is a false positive.
# BLE name matches ("FS Ext Battery") are specific enough to not need this.
_WIFI_MIN_YEAR = 2017

# BT device names — WiGLE exact-matches on `name`
_BT_QUERIES: list[str] = [
    "FS Ext Battery",
    "Flock",
    "Pigvision",
]

# WiFi SSIDs — results are filtered client-side by OUI and date
_WIFI_QUERIES: list[str] = [
    "flocknet",
]

# Polite delay between pagination requests
_PAGE_DELAY_S = 0.5

# Disk cache
_CACHE_FILE = "wigle-discovery.json"
_CACHE_TTL_S = 86_400  # 24 hours
_CACHE_VERSION = 2  # bump when Hit serialisation changes

# Signal added to all discovery-sourced hits
DISCOVERED_SIGNAL = "WIGLE_DISCOVERED"


# ---------------------------------------------------------------------------
# Hit serialisation (for disk cache)
# ---------------------------------------------------------------------------


def _hit_to_dict(h: Hit) -> dict[str, Any]:
    return {
        "mac": h.mac,
        "ssid": h.ssid,
        "device_type": h.device_type,
        "lat": h.lat,
        "lon": h.lon,
        "rssi": h.rssi,
        "first_seen": h.first_seen,
        "signals": h.signals,  # list[tuple[str,str]] — JSON-safe
        "services": h.services,
        "frequency": h.frequency,
        "capabilities": h.capabilities,
        "mfgrid": h.mfgrid,
    }


def _dict_to_hit(d: dict[str, Any]) -> Hit:
    return Hit(
        mac=d["mac"],
        ssid=d["ssid"],
        device_type=d["device_type"],
        lat=d["lat"],
        lon=d["lon"],
        rssi=d["rssi"],
        first_seen=d["first_seen"],
        signals=[tuple(s) for s in d.get("signals", [])],
        services=d.get("services", ""),
        frequency=d.get("frequency", 0),
        capabilities=d.get("capabilities", ""),
        mfgrid=d.get("mfgrid", 0),
    )


# ---------------------------------------------------------------------------
# Disk cache helpers
# ---------------------------------------------------------------------------


def _cache_path() -> Path:
    return _cache_dir() / _CACHE_FILE


def cache_age_seconds() -> float | None:
    """Return seconds since last cache write, or None if no cache exists."""
    p = _cache_path()
    if not p.exists():
        return None
    return time.time() - p.stat().st_mtime


def load_cache() -> tuple[list[Hit], float, bool] | None:
    """
    Return (hits, age_seconds, is_partial) from disk cache if usable, else None.

    Partial caches (written mid-run due to rate-limiting) are returned regardless
    of TTL so work is never thrown away — callers should note `is_partial=True`
    and offer a force-refresh to complete the dataset.
    """
    p = _cache_path()
    if not p.exists():
        return None
    age = time.time() - p.stat().st_mtime
    try:
        raw = json.loads(p.read_text(encoding="utf-8"))
        if raw.get("version") != _CACHE_VERSION:
            return None
        is_partial = bool(raw.get("partial"))
        if age > _CACHE_TTL_S and not is_partial:
            return None
        hits = [_dict_to_hit(h) for h in raw["hits"]]
        return hits, age, is_partial
    except Exception:
        return None


def save_cache(hits: list[Hit], partial: bool = False) -> None:
    """Write hits to disk cache atomically via a temp file."""
    p = _cache_path()
    tmp = p.with_suffix(".tmp")
    try:
        payload = json.dumps(
            {
                "version": _CACHE_VERSION,
                "timestamp": time.time(),
                "partial": partial,
                "hits": [_hit_to_dict(h) for h in hits],
            },
            separators=(",", ":"),
        )
        tmp.write_text(payload, encoding="utf-8")
        tmp.replace(p)
    except Exception:
        tmp.unlink(missing_ok=True)


def clear_cache() -> None:
    _cache_path().unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# Discovery stats
# ---------------------------------------------------------------------------


@dataclass
class DiscoveryStats:
    total_available: int  # WiGLE's reported total across all queries
    raw_fetched: int  # raw WiGLE rows retrieved (before filtering)
    hits_converted: int  # rows that converted to Hit objects
    from_cache: bool  # True if results came from disk cache
    cache_age_s: float  # seconds since cache was written (0 if fresh)
    error: str = ""  # last API error, if any


# ---------------------------------------------------------------------------
# WiGLE discovery engine
# ---------------------------------------------------------------------------


class WiGLEDiscovery:
    """Search WiGLE's crowdsourced database for Flock devices."""

    def __init__(
        self,
        api_name: str,
        api_token: str,
        *,
        transport: httpx.AsyncBaseTransport | None = None,
    ) -> None:
        cred = base64.b64encode(f"{api_name}:{api_token}".encode()).decode()
        self.__auth_header = f"Basic {cred}"
        self._transport = transport
        self._last_error: str = ""

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    async def discover(
        self,
        max_per_query: int = 3000,
        force_refresh: bool = False,
        progress: Callable[[int, int, str], None] | None = None,
    ) -> tuple[list[Hit], DiscoveryStats]:
        """
        Return all discovered Flock hits.

        Checks the disk cache first (24 h TTL). Pass force_refresh=True to
        bypass cache and re-fetch from WiGLE.

        Args:
            max_per_query:  Max raw WiGLE rows to retrieve per query string.
            force_refresh:  Ignore cache and re-fetch.
            progress:       Callback(raw_fetched, total_available, query_label).

        Returns:
            (hits, stats) — hits deduplicated by MAC, sorted by confidence.
        """
        if not force_refresh:
            cached = load_cache()
            if cached is not None:
                hits, age, is_partial = cached
                return hits, DiscoveryStats(
                    total_available=len(hits),
                    raw_fetched=0,
                    hits_converted=len(hits),
                    from_cache=True,
                    cache_age_s=age,
                    error="partial data (rate-limited during last fetch — press Shift+D to retry)"
                    if is_partial
                    else "",
                )

        seen: dict[str, Hit] = {}
        total_available = 0
        total_raw = 0

        async with _make_client(self._transport) as client:
            client.headers.update({"Authorization": self.__auth_header})

            rate_limited = False

            for bt_name in _BT_QUERIES:
                if rate_limited:
                    break
                lbl = f"BLE:{bt_name}"
                hits, avail, raw = await self._search_bt(
                    client,
                    bt_name,
                    max_per_query,
                    lambda f, t, _l=lbl: progress(f, t, _l) if progress else None,
                )
                total_available += avail
                total_raw += raw
                for h in hits:
                    mac = h.mac.lower()
                    if mac in seen:
                        for s in h.signals:
                            seen[mac].add_signal(*s)
                    else:
                        seen[mac] = h
                if "429" in self._last_error or "too many" in self._last_error.lower():
                    rate_limited = True
                # Save partial results after each query so work isn't lost
                save_cache(list(seen.values()), partial=True)

            for ssid in _WIFI_QUERIES:
                if rate_limited:
                    break
                lbl = f"WiFi:{ssid}"
                hits, avail, raw = await self._search_wifi(
                    client,
                    ssid,
                    max_per_query,
                    lambda f, t, _l=lbl: progress(f, t, _l) if progress else None,
                )
                total_available += avail
                total_raw += raw
                for h in hits:
                    mac = h.mac.lower()
                    if mac in seen:
                        for s in h.signals:
                            seen[mac].add_signal(*s)
                    else:
                        seen[mac] = h
                if "429" in self._last_error or "too many" in self._last_error.lower():
                    rate_limited = True

        result = sorted(seen.values(), key=lambda h: (-h.confidence, h.mac))
        # Mark as partial if we hit a rate limit mid-run
        save_cache(result, partial=bool(self._last_error))

        return result, DiscoveryStats(
            total_available=total_available,
            raw_fetched=total_raw,
            hits_converted=len(result),
            from_cache=False,
            cache_age_s=0.0,
            error=self._last_error,
        )

    # ------------------------------------------------------------------
    # BT search
    # ------------------------------------------------------------------

    async def _search_bt(
        self,
        client: httpx.AsyncClient,
        name: str,
        max_raw: int,
        progress: Callable[[int, int], None] | None,
    ) -> tuple[list[Hit], int, int]:
        """Return (hits, total_available_in_wigle, raw_rows_fetched)."""
        hits: list[Hit] = []
        search_after: str | None = None
        total_available = 0
        raw_fetched = 0

        while raw_fetched < max_raw:
            params: dict[str, Any] = {
                "name": name,
                "onlymine": "false",
                "resultsPerPage": min(100, max_raw - raw_fetched),
            }
            if search_after:
                params["search_after"] = search_after

            try:
                resp = await client.get(f"{_WIGLE_BASE}/bluetooth/search", params=params)
                if resp.status_code == 429:
                    self._last_error = f"HTTP 429 on BLE/{name} — daily quota exceeded"
                    break
                resp.raise_for_status()
                data = resp.json()
            except httpx.HTTPStatusError as exc:
                self._last_error = f"HTTP {exc.response.status_code} on BLE/{name}"
                break
            except Exception as exc:
                self._last_error = str(exc)
                break

            if not data.get("success"):
                break

            total_available = data.get("totalResults", total_available)
            results: list[dict] = data.get("results", [])
            if not results:
                break

            # Count raw rows regardless of filter — prevents runaway pagination
            raw_fetched += len(results)

            for row in results:
                h = self._bt_to_hit(row)
                if h:
                    hits.append(h)

            search_after = data.get("search_after") or data.get("searchAfter")
            if not search_after or len(results) < 100:
                break

            if progress:
                progress(raw_fetched, total_available)
            await asyncio.sleep(_PAGE_DELAY_S)

        return hits, total_available, raw_fetched

    # ------------------------------------------------------------------
    # WiFi search
    # ------------------------------------------------------------------

    async def _search_wifi(
        self,
        client: httpx.AsyncClient,
        ssid: str,
        max_raw: int,
        progress: Callable[[int, int], None] | None,
    ) -> tuple[list[Hit], int, int]:
        hits: list[Hit] = []
        search_after: str | None = None
        total_available = 0
        raw_fetched = 0

        while raw_fetched < max_raw:
            params: dict[str, Any] = {
                "ssid": ssid,
                "onlymine": "false",
                "resultsPerPage": min(100, max_raw - raw_fetched),
            }
            if search_after:
                params["search_after"] = search_after

            try:
                resp = await client.get(f"{_WIGLE_BASE}/network/search", params=params)
                if resp.status_code == 429:
                    self._last_error = f"HTTP 429 on WiFi/{ssid} — daily quota exceeded"
                    break
                resp.raise_for_status()
                data = resp.json()
            except httpx.HTTPStatusError as exc:
                self._last_error = f"HTTP {exc.response.status_code} on WiFi/{ssid}"
                break
            except Exception as exc:
                self._last_error = str(exc)
                break

            if not data.get("success"):
                break

            total_available = data.get("totalResults", total_available)
            results: list[dict] = data.get("results", [])
            if not results:
                break

            raw_fetched += len(results)

            for row in results:
                h = self._wifi_to_hit(row)
                if h:
                    hits.append(h)

            search_after = data.get("search_after") or data.get("searchAfter")
            if not search_after or len(results) < 100:
                break

            if progress:
                progress(raw_fetched, total_available)
            await asyncio.sleep(_PAGE_DELAY_S)

        return hits, total_available, raw_fetched

    # ------------------------------------------------------------------
    # Result → Hit converters
    # ------------------------------------------------------------------

    def _bt_to_hit(self, row: dict[str, Any]) -> Hit | None:
        mac = (row.get("netid") or "").lower().strip()
        if not mac:
            return None
        # No date filter for BLE — "FS Ext Battery" is too specific to have
        # meaningful false positives, and Flock deployed cameras from ~2019.
        name = row.get("name") or row.get("ssid") or ""
        lat = float(row.get("trilat") or 0)
        lon = float(row.get("trilong") or 0)
        caps = row.get("capabilities") or []
        cap_str = caps[0] if isinstance(caps, list) and caps else str(caps)
        mfgrid = row.get("mfgrId") or 0
        freq = int(row.get("device") or 0)

        h = analyze(
            mac=mac,
            ssid=name,
            type="E",
            lat=lat,
            lon=lon,
            rssi=int(row.get("qos") or 0),
            first_seen=row.get("firsttime") or "",
            frequency=freq,
            capabilities=cap_str,
            mfgrid=int(mfgrid) if mfgrid else 0,
        )
        if h is None:
            h = Hit(
                mac=mac,
                ssid=name,
                device_type="E",
                lat=lat,
                lon=lon,
                rssi=int(row.get("qos") or 0),
                first_seen=row.get("firsttime") or "",
                frequency=freq,
                capabilities=cap_str,
                mfgrid=int(mfgrid) if mfgrid else 0,
            )
        h.add_signal(DISCOVERED_SIGNAL, self._location_str(row))
        return h

    def _wifi_to_hit(self, row: dict[str, Any]) -> Hit | None:
        mac = (row.get("netid") or "").lower().strip()
        if not mac:
            return None
        ssid = row.get("ssid") or ""
        last_time = row.get("lasttime") or ""
        if last_time and int(last_time[:4]) < _WIFI_MIN_YEAR:
            return None
        oui = mac[:8]
        if ssid.lower() == "flocknet" and oui not in sig.FLOCK_BACKHAUL_OUIS:
            return None
        lat = float(row.get("trilat") or 0)
        lon = float(row.get("trilong") or 0)
        freq = int(row.get("frequency") or row.get("channel") or 0)
        enc = row.get("encryption") or ""

        h = analyze(
            mac=mac,
            ssid=ssid,
            type="W",
            lat=lat,
            lon=lon,
            rssi=int(row.get("qos") or 0),
            first_seen=row.get("firsttime") or "",
            frequency=freq,
            capabilities=enc,
        )
        if h is None:
            h = Hit(
                mac=mac,
                ssid=ssid,
                device_type="W",
                lat=lat,
                lon=lon,
                rssi=int(row.get("qos") or 0),
                first_seen=row.get("firsttime") or "",
                frequency=freq,
                capabilities=enc,
            )
        h.add_signal(DISCOVERED_SIGNAL, self._location_str(row))
        return h

    @staticmethod
    def _location_str(row: dict[str, Any]) -> str:
        parts = [row.get("city"), row.get("region"), row.get("country")]
        return ", ".join(p for p in parts if p)


# ---------------------------------------------------------------------------
# Factory
# ---------------------------------------------------------------------------


def build_discovery(
    wigle_name: str = "",
    wigle_token: str = "",
) -> WiGLEDiscovery | None:
    """Return a WiGLEDiscovery instance if credentials are available, else None."""
    name = wigle_name or os.environ.get("WIGLE_API_NAME", "")
    token = wigle_token or os.environ.get("WIGLE_API_TOKEN", "")
    if not (name and token):
        cfg = load_config()
        name = name or cfg.get("wigle_api_name", "")
        token = token or cfg.get("wigle_api_token", "")
    if name and token:
        return WiGLEDiscovery(name, token)
    return None
