"""Tests for gps_source.py — Meshtastic position decoding and local tracking.

These never import the real `meshtastic` package: the decode helper is pure and
the source is exercised with a fake interface (subscribe disabled).
"""

from __future__ import annotations

from flockdar import gps_source as gs


class FakeIface:
    def __init__(self, num=None, position=None):
        self._info = {"num": num, "position": position or {}}
        self.closed = False

    def getMyNodeInfo(self):
        return self._info

    def close(self):
        self.closed = True


def _pkt(frm, lat=None, lon=None, lat_i=None, lon_i=None):
    pos = {}
    if lat is not None:
        pos["latitude"] = lat
    if lon is not None:
        pos["longitude"] = lon
    if lat_i is not None:
        pos["latitudeI"] = lat_i
    if lon_i is not None:
        pos["longitudeI"] = lon_i
    return {"from": frm, "decoded": {"position": pos}}


class TestPositionFromPacket:
    def test_float_fields(self):
        assert gs.position_from_packet(_pkt(1, lat=39.9416, lon=-75.1758)) == (39.9416, -75.1758)

    def test_integer_fields(self):
        pos = gs.position_from_packet(_pkt(1, lat_i=399416000, lon_i=-751758000))
        assert pos is not None
        assert round(pos[0], 4) == 39.9416 and round(pos[1], 4) == -75.1758

    def test_float_preferred_over_integer(self):
        assert gs.position_from_packet(
            _pkt(1, lat=1.0, lon=2.0, lat_i=999, lon_i=999)
        ) == (1.0, 2.0)

    def test_missing_position_is_none(self):
        assert gs.position_from_packet({"decoded": {}}) is None
        assert gs.position_from_packet({}) is None
        assert gs.position_from_packet(None) is None

    def test_zero_fix_is_none(self):
        assert gs.position_from_packet(_pkt(1, lat=0.0, lon=0.0)) is None


class TestMeshtasticPositionSource:
    def test_seeds_from_local_node(self):
        iface = FakeIface(num=7, position={"latitude": 12.0, "longitude": 34.0})
        src = gs.MeshtasticPositionSource(interface=iface, subscribe=False)
        assert src.current() == (12.0, 34.0)

    def test_no_seed_when_node_has_no_fix(self):
        src = gs.MeshtasticPositionSource(interface=FakeIface(num=7), subscribe=False)
        assert src.current() is None

    def test_receive_updates_position(self):
        src = gs.MeshtasticPositionSource(interface=FakeIface(num=7), subscribe=False)
        src._on_receive(packet=_pkt(7, lat=1.5, lon=2.5))
        assert src.current() == (1.5, 2.5)

    def test_local_only_ignores_foreign_node(self):
        iface = FakeIface(num=7, position={"latitude": 12.0, "longitude": 34.0})
        src = gs.MeshtasticPositionSource(interface=iface, subscribe=False)
        src._on_receive(packet=_pkt(999, lat=88.0, lon=88.0))  # different node
        assert src.current() == (12.0, 34.0)  # unchanged

    def test_local_only_accepts_own_node(self):
        src = gs.MeshtasticPositionSource(interface=FakeIface(num=7), subscribe=False)
        src._on_receive(packet=_pkt(7, lat=5.0, lon=6.0))
        assert src.current() == (5.0, 6.0)

    def test_accepts_any_node_when_local_only_false(self):
        src = gs.MeshtasticPositionSource(
            interface=FakeIface(num=7), subscribe=False, local_only=False
        )
        src._on_receive(packet=_pkt(999, lat=5.0, lon=6.0))
        assert src.current() == (5.0, 6.0)

    def test_close_closes_interface(self):
        iface = FakeIface(num=7)
        gs.MeshtasticPositionSource(interface=iface, subscribe=False).close()
        assert iface.closed
