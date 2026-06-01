"""Tests for detect.py — analyze(), cluster_hits(), confidence scoring."""

from __future__ import annotations

import pytest

from flockdar import signatures as sig
from flockdar.detect import Hit, Cluster, analyze, cluster_hits, single_clusters
from tests.conftest import (
    MAC_BLE_NAME,
    MAC_DIRECT,
    MAC_EERO,
    MAC_EERO_HIDDEN,
    MAC_EERO_NAMED,
    MAC_EX,
    MAC_EX2,
    MAC_FLOCK_CAM,
    MAC_FLOCK_CHIP,
    MAC_MFGRID,
    MAC_PENGUIN,
    MAC_RAVEN,
    MAC_SURVEILLANCE,
    MAC_UNKNOWN,
    SSID_FLOCK_CAM,
    make_hit,
)


# ---------------------------------------------------------------------------
# analyze() — individual signal firing
# ---------------------------------------------------------------------------

class TestAnalyzeFlockCamera:
    def test_mac_validated_ssid_is_high(self) -> None:
        h = analyze(mac=MAC_FLOCK_CAM, ssid=SSID_FLOCK_CAM, type="W")
        assert h is not None
        assert h.confidence == 3
        labels = {l for l, _ in h.signals}
        assert "FLOCK_CAMERA_SSID" in labels

    def test_oui_corroborated_ssid_is_high(self) -> None:
        h = analyze(mac=MAC_FLOCK_CHIP, ssid="Flock-AABBCC", type="W")
        assert h is not None
        assert h.confidence == 3
        labels = {l for l, _ in h.signals}
        assert "FLOCK_CAMERA_SSID" in labels

    def test_unknown_oui_ssid_pattern_is_medium(self) -> None:
        h = analyze(mac=MAC_EX, ssid="Flock-AABBCC", type="W")
        assert h is not None
        assert h.confidence == 2
        labels = {l for l, _ in h.signals}
        assert "FLOCK_CAMERA_SSID_PATTERN" in labels

    def test_wrong_mac_suffix_unknown_oui(self) -> None:
        h = analyze(mac="02:00:00:00:00:04", ssid="Flock-AABBCC", type="W")
        assert h is not None
        labels = {l for l, _ in h.signals}
        assert "FLOCK_CAMERA_SSID_PATTERN" in labels


class TestAnalyzeFlocknet:
    def test_flocknet_on_eero_is_high(self) -> None:
        h = analyze(mac=MAC_EERO, ssid="flocknet", type="W")
        assert h is not None
        assert h.confidence == 3
        labels = {l for l, _ in h.signals}
        assert "FLOCKNET_SSID" in labels

    def test_eero_hidden_ssid_is_medium(self) -> None:
        h = analyze(mac=MAC_EERO_HIDDEN, ssid="", type="W")
        assert h is not None
        assert h.confidence == 1  # BACKHAUL_OUI_HIDDEN is LOW tier
        labels = {l for l, _ in h.signals}
        assert "BACKHAUL_OUI_HIDDEN" in labels

    def test_eero_named_non_flock_ssid_returns_none(self) -> None:
        h = analyze(mac=MAC_EERO_NAMED, ssid="FiOS-G7H5U", type="W")
        assert h is None


class TestAnalyzePenguin:
    def test_penguin_ble_ssid_is_high(self) -> None:
        h = analyze(mac=MAC_PENGUIN, ssid="Penguin-1069698414", type="E")
        assert h is not None
        assert h.confidence == 3
        labels = {l for l, _ in h.signals}
        assert "PENGUIN_BLE_SSID" in labels

    def test_mfgrid_2504_is_medium(self) -> None:
        h = analyze(mac=MAC_MFGRID, ssid="1069698414", type="E", mfgrid=2504)
        assert h is not None
        assert h.confidence == 2
        labels = {l for l, _ in h.signals}
        assert "FLOCK_MFGRID" in labels

    def test_penguin_ssid_and_mfgrid_is_high(self) -> None:
        h = analyze(
            mac=MAC_PENGUIN, ssid="Penguin-1069698414",
            type="E", mfgrid=2504,
        )
        assert h is not None
        assert h.confidence == 3


class TestAnalyzeWiFiFingerprint:
    def test_oui_wpa2_channel1_is_medium(self) -> None:
        h = analyze(
            mac="70:c9:4e:00:00:03", ssid="",
            type="W", frequency=2412,
            capabilities="[WPA2-PSK-CCMP-128][RSN-PSK-CCMP-128][ESS]",
        )
        assert h is not None
        labels = {l for l, _ in h.signals}
        assert "FLOCK_WIFI_FP" in labels

    def test_wrong_capabilities_no_fp(self) -> None:
        h = analyze(
            mac="70:c9:4e:00:00:03", ssid="",
            type="W", frequency=2412,
            capabilities="[WPA2-PSK-CCMP-128][RSN-PSK-CCMP-128][ESS][WPS]",
        )
        if h:
            labels = {l for l, _ in h.signals}
            assert "FLOCK_WIFI_FP" not in labels

    def test_wrong_channel_no_fp(self) -> None:
        h = analyze(
            mac="70:c9:4e:00:00:03", ssid="",
            type="W", frequency=5765,
            capabilities="[WPA2-PSK-CCMP-128][RSN-PSK-CCMP-128][ESS]",
        )
        if h:
            labels = {l for l, _ in h.signals}
            assert "FLOCK_WIFI_FP" not in labels


class TestAnalyzeRavenUUIDs:
    def test_raven_uuid_high_is_high(self) -> None:
        h = analyze(
            mac=MAC_SURVEILLANCE, ssid="FS Ext Battery",
            type="E",
            services="00003100-0000-1000-8000-00805f9b34fb",
        )
        assert h is not None
        assert h.confidence == 3
        labels = {l for l, _ in h.signals}
        assert "RAVEN_UUID_HIGH" in labels

    def test_old_uuid_alone_is_low(self) -> None:
        h = analyze(
            mac=MAC_EX, ssid="SomeDevice",
            type="E",
            services="0000180a-0000-1000-8000-00805f9b34fb",
        )
        if h:
            assert h.confidence == 1


class TestAnalyzeDirectOUI:
    def test_direct_flock_oui_is_high(self) -> None:
        h = analyze(mac=MAC_DIRECT, ssid="", type="W")
        assert h is not None
        assert h.confidence == 3
        labels = {l for l, _ in h.signals}
        assert "FLOCK_DIRECT_OUI" in labels


class TestAnalyzeNoMatch:
    def test_random_device_returns_none(self) -> None:
        h = analyze(mac=MAC_UNKNOWN, ssid="MyHomeWifi", type="W")
        assert h is None

    def test_empty_mac_returns_none(self) -> None:
        h = analyze(mac="", ssid="", type="W")
        assert h is None


class TestAnalyzeSurveillanceOUI:
    def test_axis_oui_is_low(self) -> None:
        h = analyze(mac=MAC_RAVEN, ssid="", type="W")
        assert h is not None
        assert h.confidence == 1
        labels = {l for l, _ in h.signals}
        assert "SURVEILLANCE_OUI" in labels


# ---------------------------------------------------------------------------
# Hit.add_signal deduplication
# ---------------------------------------------------------------------------

class TestHitSignals:
    def test_no_duplicates(self) -> None:
        h = make_hit()
        h.add_signal("CHIP_OUI", "70:c9:4e")
        h.add_signal("CHIP_OUI", "70:c9:4e")
        assert h.signals.count(("CHIP_OUI", "70:c9:4e")) == 1

    def test_same_label_different_detail(self) -> None:
        h = make_hit()
        h.add_signal("CHIP_OUI", "70:c9:4e")
        h.add_signal("CHIP_OUI", "08:3a:88")
        assert len(h.signals) == 2

    def test_signals_str(self) -> None:
        h = make_hit()
        h.add_signal("CHIP_OUI", "70:c9:4e")
        h.add_signal("FLOCKNET_SSID", "")
        s = h.signals_str()
        assert "CHIP_OUI(70:c9:4e)" in s
        assert "FLOCKNET_SSID" in s


# ---------------------------------------------------------------------------
# cluster_hits()
# ---------------------------------------------------------------------------

class TestClusterHits:
    def test_empty(self) -> None:
        assert cluster_hits([]) == []

    def test_single(self) -> None:
        h = make_hit(lat=40.0, lon=-74.0)
        clusters = cluster_hits([h])
        assert len(clusters) == 1
        assert clusters[0].hits == [h]

    def test_two_within_radius(self) -> None:
        h1 = make_hit(lat=40.00000, lon=-74.00000)
        h2 = make_hit(lat=40.00010, lon=-74.00005)
        clusters = cluster_hits([h1, h2], radius_m=75.0)
        assert len(clusters) == 1
        assert len(clusters[0].hits) == 2

    def test_two_outside_radius(self) -> None:
        h1 = make_hit(lat=40.00000, lon=-74.00000)
        h2 = make_hit(lat=40.00200, lon=-74.00000)
        clusters = cluster_hits([h1, h2], radius_m=75.0)
        assert len(clusters) == 2

    def test_sorted_by_confidence_desc(self) -> None:
        low = make_hit(lat=40.0, lon=-74.0)
        low.add_signal("CHIP_OUI", "70:c9:4e")

        high = make_hit(lat=40.00, lon=-75.00)
        high.add_signal("FLOCKNET_SSID", "flocknet")

        clusters = cluster_hits([low, high])
        assert clusters[0].confidence == 3

    def test_no_location_not_clustered_together(self) -> None:
        h1 = make_hit(lat=0.0, lon=0.0)
        h2 = make_hit(lat=0.0, lon=0.0)
        clusters = cluster_hits([h1, h2], radius_m=75.0)
        assert len(clusters) == 2


class TestSingleClusters:
    def test_each_hit_gets_own_cluster(self) -> None:
        hits = [make_hit() for _ in range(3)]
        clusters = single_clusters(hits)
        assert len(clusters) == 3
        for i, c in enumerate(clusters):
            assert c.hits == [hits[i]]


# ---------------------------------------------------------------------------
# Cluster properties
# ---------------------------------------------------------------------------

class TestClusterProperties:
    def _cluster_with_hits(self, *hits: Hit) -> Cluster:
        return Cluster(list(hits))

    def test_confidence_is_max(self) -> None:
        low = make_hit()
        low.add_signal("CHIP_OUI", "x")
        high = make_hit()
        high.add_signal("FLOCKNET_SSID", "y")
        c = self._cluster_with_hits(low, high)
        assert c.confidence == 3

    def test_lat_lon_centroid(self) -> None:
        h1 = make_hit(lat=40.0, lon=-74.0)
        h2 = make_hit(lat=40.02, lon=-74.02)
        c = self._cluster_with_hits(h1, h2)
        assert abs(c.lat - 40.01) < 1e-6
        assert abs(c.lon - (-74.01)) < 1e-6

    def test_label_uses_most_common_ssid(self) -> None:
        h1 = make_hit(ssid="flocknet")
        h2 = make_hit(ssid="flocknet")
        h3 = make_hit(ssid="other")
        c = self._cluster_with_hits(h1, h2, h3)
        assert c.label == "flocknet"

    def test_mac_list(self) -> None:
        h1 = make_hit(mac=MAC_EX)
        h2 = make_hit(mac=MAC_EX2)
        c = self._cluster_with_hits(h1, h2)
        assert MAC_EX in c.mac_list
        assert MAC_EX2 in c.mac_list

    def test_maps_url(self) -> None:
        h = make_hit(lat=40.0, lon=-74.0)
        c = self._cluster_with_hits(h)
        assert "maps.google.com" in c.maps_url
        assert "40.0" in c.maps_url

    def test_streetview_url(self) -> None:
        h = make_hit(lat=40.0, lon=-74.0)
        c = self._cluster_with_hits(h)
        assert "layer=c" in c.streetview_url
