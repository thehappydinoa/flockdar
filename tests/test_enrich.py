"""Tests for enrich.py — enrichers, config, cache, async behaviour."""

from __future__ import annotations

import json
import sys
import zipfile
from io import BytesIO
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import httpx
import pytest

from flockdar import enrich as enrich_mod
from flockdar.enrich import (
    ALPRWatchEnricher,
    OverpassEnricher,
    WiGLEEnricher,
    _BoundedCache,
    _dist_m,
    build_enrichers,
    enrich_hits_async,
    load_config,
    save_config,
)
from tests.conftest import make_hit, make_kmz


# ---------------------------------------------------------------------------
# _BoundedCache
# ---------------------------------------------------------------------------

class TestBoundedCache:
    def test_basic_set_get(self) -> None:
        c = _BoundedCache(maxsize=3)
        c["a"] = 1
        assert c["a"] == 1

    def test_evicts_oldest_when_full(self) -> None:
        c = _BoundedCache(maxsize=2)
        c["x"] = 1
        c["y"] = 2
        c["z"] = 3          # "x" should be evicted
        assert "x" not in c
        assert "y" in c
        assert "z" in c

    def test_update_does_not_duplicate(self) -> None:
        c = _BoundedCache(maxsize=2)
        c["a"] = 1
        c["a"] = 2          # overwrite, not a new entry
        assert len(c) == 1
        assert c["a"] == 2

    def test_never_exceeds_maxsize(self) -> None:
        c = _BoundedCache(maxsize=5)
        for i in range(20):
            c[i] = i
        assert len(c) <= 5


# ---------------------------------------------------------------------------
# _dist_m
# ---------------------------------------------------------------------------

class TestDistM:
    def test_same_point_is_zero(self) -> None:
        assert _dist_m(40.0, -74.0, 40.0, -74.0) == pytest.approx(0.0)

    def test_known_distance(self) -> None:
        # ~111 m per 0.001 degree lat at this latitude
        d = _dist_m(40.0, -74.0, 40.001, -74.0)
        assert 100 < d < 120

    def test_symmetry(self) -> None:
        d1 = _dist_m(40.0, -74.0, 40.02, -74.02)
        d2 = _dist_m(40.02, -74.02, 40.0, -74.0)
        assert d1 == pytest.approx(d2)


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

class TestConfig:
    def test_roundtrip(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        cfg_file = tmp_path / "flock-wigle" / "config.json"
        monkeypatch.setattr(enrich_mod, "_config_path", lambda: cfg_file)
        cfg_file.parent.mkdir(parents=True, exist_ok=True)

        save_config({"wigle_api_name": "alice", "wigle_api_token": "secret"})
        loaded = load_config()
        assert loaded["wigle_api_name"] == "alice"
        assert loaded["wigle_api_token"] == "secret"

    def test_missing_file_returns_empty(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setattr(enrich_mod, "_config_path", lambda: tmp_path / "nonexistent.json")
        assert load_config() == {}

    def test_corrupt_json_returns_empty(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        cfg_file = tmp_path / "config.json"
        cfg_file.write_text("not json", encoding="utf-8")
        monkeypatch.setattr(enrich_mod, "_config_path", lambda: cfg_file)
        assert load_config() == {}

    @pytest.mark.skipif(sys.platform == "win32", reason="Unix permissions only")
    def test_permissions_set_to_600(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        cfg_file = tmp_path / "config.json"
        monkeypatch.setattr(enrich_mod, "_config_path", lambda: cfg_file)
        save_config({"key": "val"})
        assert (cfg_file.stat().st_mode & 0o777) == 0o600

    @pytest.mark.skipif(sys.platform == "win32", reason="Unix permissions only")
    def test_load_fixes_world_readable(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        cfg_file = tmp_path / "config.json"
        cfg_file.write_text('{"k":"v"}', encoding="utf-8")
        cfg_file.chmod(0o644)
        monkeypatch.setattr(enrich_mod, "_config_path", lambda: cfg_file)
        load_config()
        assert (cfg_file.stat().st_mode & 0o777) == 0o600


# ---------------------------------------------------------------------------
# OverpassEnricher
# ---------------------------------------------------------------------------

def _overpass_transport(elements: list) -> httpx.MockTransport:
    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(200, json={"elements": elements})
    return httpx.MockTransport(handler)


def _overpass_error_transport() -> httpx.MockTransport:
    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(500)
    return httpx.MockTransport(handler)


class TestOverpassEnricher:
    async def test_no_location_returns_empty(self) -> None:
        e = OverpassEnricher(transport=_overpass_transport([]))
        result = await e.enrich(make_hit(lat=0.0, lon=0.0))
        assert result == []

    async def test_no_nearby_nodes_returns_empty(self) -> None:
        e = OverpassEnricher(transport=_overpass_transport([]))
        result = await e.enrich(make_hit(lat=40.0, lon=-74.0))
        assert result == []

    async def test_nearby_node_returns_signal(self) -> None:
        nodes = [{"id": 1, "lat": 40.0001, "lon": -74.0001, "tags": {"name": "Cam1"}}]
        e = OverpassEnricher(transport=_overpass_transport(nodes))
        result = await e.enrich(make_hit(lat=40.0, lon=-74.0))
        assert len(result) == 1
        label, detail = result[0]
        assert label == "OSM_ALPR_NEARBY"
        assert "Cam1" in detail

    async def test_includes_operator_in_detail(self) -> None:
        nodes = [{
            "id": 2, "lat": 40.0001, "lon": -74.0001,
            "tags": {"name": "Cam2", "operator": "Flock Safety"},
        }]
        e = OverpassEnricher(transport=_overpass_transport(nodes))
        result = await e.enrich(make_hit(lat=40.0, lon=-74.0))
        label, detail = result[0]
        assert "Flock Safety" in detail

    async def test_cache_reused_for_same_location(self) -> None:
        call_count = 0

        def handler(request: httpx.Request) -> httpx.Response:
            nonlocal call_count
            call_count += 1
            return httpx.Response(200, json={"elements": []})

        transport = httpx.MockTransport(handler)
        e = OverpassEnricher(transport=transport)
        hit = make_hit(lat=40.00001, lon=-74.00001)
        await e.enrich(hit)
        await e.enrich(hit)  # second call — should hit cache
        assert call_count == 1

    async def test_http_error_returns_empty(self) -> None:
        e = OverpassEnricher(transport=_overpass_error_transport())
        result = await e.enrich(make_hit(lat=40.0, lon=-74.0))
        assert result == []

    async def test_closest_node_is_returned(self) -> None:
        nodes = [
            {"id": 1, "lat": 40.0002, "lon": -74.0002, "tags": {"name": "Near"}},
            {"id": 2, "lat": 40.01, "lon": -74.01, "tags": {"name": "Far"}},
        ]
        e = OverpassEnricher(transport=_overpass_transport(nodes))
        result = await e.enrich(make_hit(lat=40.0, lon=-74.0))
        _, detail = result[0]
        assert "Near" in detail


# ---------------------------------------------------------------------------
# ALPRWatchEnricher
# ---------------------------------------------------------------------------

class TestALPRWatchEnricher:
    def test_parse_kmz_extracts_cameras(self, sample_kmz_path: Path) -> None:
        cameras = ALPRWatchEnricher._parse_kmz(sample_kmz_path)
        assert len(cameras) == 3
        lats = [c[0] for c in cameras]
        assert pytest.approx(40.0016, abs=1e-4) in lats

    def test_parse_kmz_empty_returns_empty(self, tmp_path: Path) -> None:
        kmz = tmp_path / "empty.kmz"
        kmz.write_bytes(make_kmz([]))
        cameras = ALPRWatchEnricher._parse_kmz(kmz)
        assert cameras == []

    def test_parse_kmz_no_kml_returns_empty(self, tmp_path: Path) -> None:
        kmz = tmp_path / "nokml.kmz"
        buf = BytesIO()
        with zipfile.ZipFile(buf, "w") as zf:
            zf.writestr("readme.txt", "not a kml")
        kmz.write_bytes(buf.getvalue())
        cameras = ALPRWatchEnricher._parse_kmz(kmz)
        assert cameras == []

    async def test_enrich_no_location_returns_empty(self, sample_kmz_path: Path) -> None:
        e = ALPRWatchEnricher()
        e._loaded = True
        e._cameras = ALPRWatchEnricher._parse_kmz(sample_kmz_path)
        result = await e.enrich(make_hit(lat=0.0, lon=0.0))
        assert result == []

    async def test_enrich_nearby_camera(self, sample_kmz_path: Path) -> None:
        e = ALPRWatchEnricher(radius_m=500.0)
        e._loaded = True
        e._cameras = ALPRWatchEnricher._parse_kmz(sample_kmz_path)
        # Camera A is at (40.0016, -74.0008) — hit is very close
        result = await e.enrich(make_hit(lat=40.0017, lon=-74.0009))
        assert len(result) == 1
        label, detail = result[0]
        assert label == "ALPRWATCH_NEARBY"
        assert "Flock Camera A" in detail

    async def test_enrich_camera_out_of_radius(self, sample_kmz_path: Path) -> None:
        e = ALPRWatchEnricher(radius_m=50.0)
        e._loaded = True
        e._cameras = ALPRWatchEnricher._parse_kmz(sample_kmz_path)
        # All sample cameras are >500m from this hit — nothing within 50m
        result = await e.enrich(make_hit(lat=40.0, lon=-74.0))
        assert len(result) == 0

    async def test_load_guard_prevents_double_download(
        self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, sample_kmz_path: Path
    ) -> None:
        call_count = 0

        async def fake_download(self_inner: ALPRWatchEnricher, path: Path) -> None:
            nonlocal call_count
            call_count += 1
            path.write_bytes(sample_kmz_path.read_bytes())

        monkeypatch.setattr(ALPRWatchEnricher, "_download", fake_download)
        monkeypatch.setattr(enrich_mod, "_cache_dir", lambda: tmp_path)

        e = ALPRWatchEnricher()
        # Simulate two concurrent enrichment calls
        hit = make_hit(lat=40.0017, lon=-74.0009)
        await enrich_hits_async([hit, make_hit(lat=40.0018, lon=-74.001)], [e])
        assert call_count == 1  # downloaded only once despite concurrent calls


# ---------------------------------------------------------------------------
# WiGLEEnricher
# ---------------------------------------------------------------------------

def _wigle_transport(status: int, body: dict) -> httpx.MockTransport:
    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(status, json=body)
    return httpx.MockTransport(handler)


class TestWiGLEEnricher:
    async def test_found_returns_wigle_seen(self) -> None:
        body = {
            "results": [{
                "firsttime": "2024-01-15T12:00:00Z",
                "lasttime":  "2025-06-01T08:00:00Z",
                "trilat": "40.0",
                "trilong": "-74.0",
                "locationData": [{"total": 42}],
            }]
        }
        e = WiGLEEnricher("name", "token", transport=_wigle_transport(200, body))
        result = await e.enrich(make_hit(mac="70:c9:4e:00:00:01"))
        assert len(result) == 1
        label, detail = result[0]
        assert label == "WIGLE_SEEN"
        assert "2024-01-15" in detail
        assert "42" in detail

    async def test_not_found_404(self) -> None:
        e = WiGLEEnricher("name", "token", transport=_wigle_transport(404, {}))
        result = await e.enrich(make_hit(mac="70:c9:4e:00:00:01"))
        assert result == [("WIGLE_NOT_FOUND", "not in WiGLE DB")]

    async def test_empty_results(self) -> None:
        e = WiGLEEnricher("name", "token", transport=_wigle_transport(200, {"results": []}))
        result = await e.enrich(make_hit(mac="70:c9:4e:00:00:01"))
        assert result == [("WIGLE_NOT_FOUND", "no results")]

    async def test_no_mac_returns_empty(self) -> None:
        e = WiGLEEnricher("name", "token", transport=_wigle_transport(200, {}))
        result = await e.enrich(make_hit(mac=""))
        assert result == []

    async def test_auth_header_not_accessible(self) -> None:
        e = WiGLEEnricher("alice", "s3cret", transport=_wigle_transport(200, {"results": []}))
        assert not hasattr(e, "_auth_header")        # not name-mangled accessible name
        assert not hasattr(e, "auth_header")

    async def test_request_includes_mac_param(self) -> None:
        captured: list[httpx.Request] = []

        def handler(req: httpx.Request) -> httpx.Response:
            captured.append(req)
            return httpx.Response(200, json={"results": []})

        e = WiGLEEnricher("n", "t", transport=httpx.MockTransport(handler))
        await e.enrich(make_hit(mac="70:c9:4e:00:00:01"))
        assert captured
        assert "70" in str(captured[0].url)

    async def test_rate_limit_respected(self) -> None:
        import time
        call_times: list[float] = []

        def handler(req: httpx.Request) -> httpx.Response:
            call_times.append(time.monotonic())
            return httpx.Response(200, json={"results": []})

        # Override rate limit to something fast for testing
        original = enrich_mod._WIGLE_RATELIM
        enrich_mod._WIGLE_RATELIM = 0.05
        try:
            e = WiGLEEnricher("n", "t", transport=httpx.MockTransport(handler))
            hit1 = make_hit(mac="02:00:00:00:00:05")
            hit2 = make_hit(mac="02:00:00:00:00:06")
            await enrich_hits_async([hit1, hit2], [e])
        finally:
            enrich_mod._WIGLE_RATELIM = original

        assert len(call_times) == 2
        # Second request must be at least rate_limit seconds after first
        assert call_times[1] - call_times[0] >= 0.04


# ---------------------------------------------------------------------------
# enrich_hits_async — integration
# ---------------------------------------------------------------------------

class TestEnrichHitsAsync:
    async def test_callback_called_for_each_hit(self) -> None:
        hits = [make_hit() for _ in range(3)]
        called: list[int] = []

        async def fake_enrich(h):
            return []

        class FakeEnricher(OverpassEnricher):
            async def enrich(self, hit):
                return await fake_enrich(hit)

        await enrich_hits_async(hits, [FakeEnricher()], callback=lambda h: called.append(1))
        assert len(called) == 3

    async def test_signals_added_in_place(self) -> None:
        hit = make_hit(lat=40.0, lon=-74.0)
        nodes = [{"id": 1, "lat": 40.0001, "lon": -74.0001, "tags": {"name": "TestCam"}}]
        e = OverpassEnricher(transport=_overpass_transport(nodes))
        await enrich_hits_async([hit], [e])
        labels = {l for l, _ in hit.signals}
        assert "OSM_ALPR_NEARBY" in labels

    async def test_enricher_exception_does_not_abort(self) -> None:
        class BrokenEnricher(OverpassEnricher):
            async def enrich(self, hit):
                raise RuntimeError("boom")

        hit = make_hit(lat=40.0, lon=-74.0)
        # Should not raise
        await enrich_hits_async([hit], [BrokenEnricher()])

    async def test_no_location_skips_enricher_but_calls_callback(self) -> None:
        called: list[bool] = []
        hit = make_hit(lat=0.0, lon=0.0)
        await enrich_hits_async(
            [hit],
            [OverpassEnricher(transport=_overpass_transport([]))],
            callback=lambda h: called.append(True),
        )
        assert called == [True]


# ---------------------------------------------------------------------------
# build_enrichers
# ---------------------------------------------------------------------------

class TestBuildEnrichers:
    def test_default_includes_overpass_and_alprwatch(self) -> None:
        enrichers = build_enrichers()
        names = [e.name for e in enrichers]
        assert "OSM/DeFlock" in names
        assert "ALPRWatch" in names

    def test_wigle_included_when_credentials_provided(self) -> None:
        enrichers = build_enrichers(wigle_name="alice", wigle_token="secret")
        names = [e.name for e in enrichers]
        assert "WiGLE" in names

    def test_wigle_excluded_when_no_credentials(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        monkeypatch.delenv("WIGLE_API_NAME",  raising=False)
        monkeypatch.delenv("WIGLE_API_TOKEN", raising=False)
        monkeypatch.setattr(enrich_mod, "load_config", lambda: {})
        enrichers = build_enrichers()
        names = [e.name for e in enrichers]
        assert "WiGLE" not in names

    def test_wigle_reads_env_vars(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setenv("WIGLE_API_NAME",  "envuser")
        monkeypatch.setenv("WIGLE_API_TOKEN", "envtoken")
        enrichers = build_enrichers()
        names = [e.name for e in enrichers]
        assert "WiGLE" in names

    def test_overpass_disabled(self) -> None:
        enrichers = build_enrichers(overpass=False)
        names = [e.name for e in enrichers]
        assert "OSM/DeFlock" not in names

    def test_alprwatch_disabled(self) -> None:
        enrichers = build_enrichers(alprwatch=False)
        names = [e.name for e in enrichers]
        assert "ALPRWatch" not in names
