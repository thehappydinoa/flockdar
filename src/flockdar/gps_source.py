"""
External GPS position sources for live ingestion.

The ESP32 scanner can carry its own GPS (it emits `gps` lines), but on a
Raspberry Pi build it is often easier to take position from a Meshtastic node
already in the vehicle. `MeshtasticPositionSource` connects to a node over USB
serial or TCP, tracks the *local* node's latest fix, and exposes it as a
`current() -> (lat, lon) | None` callable that the serial ingest stamps onto
each detection.

The `meshtastic` package is an optional dependency (extra `meshtastic`):

    uv sync --extra meshtastic        # or: pip install "flockdar[meshtastic]"

It is imported lazily so the rest of flockdar works without it.
"""

from __future__ import annotations

import threading
from typing import Any, Optional, Protocol

Position = tuple[float, float]


class PositionSource(Protocol):
    """Anything that can report a current (lat, lon) fix."""

    def current(self) -> Optional[Position]: ...
    def close(self) -> None: ...


# ---------------------------------------------------------------------------
# Meshtastic
# ---------------------------------------------------------------------------

_POSITION_TOPIC = "meshtastic.receive.position"


def position_from_packet(packet: Any) -> Optional[Position]:
    """Extract (lat, lon) from a Meshtastic position packet, or None.

    Prefers the library's float `latitude`/`longitude` fields, falling back to
    the raw `latitudeI`/`longitudeI` integers (degrees x 1e7). A 0/0 fix (no
    lock) is treated as no position.
    """
    try:
        pos = packet["decoded"]["position"]
    except (KeyError, TypeError):
        return None

    lat = pos.get("latitude")
    lon = pos.get("longitude")
    if lat is None and pos.get("latitudeI") is not None:
        lat = pos["latitudeI"] / 1e7
    if lon is None and pos.get("longitudeI") is not None:
        lon = pos["longitudeI"] / 1e7
    if lat is None or lon is None:
        return None
    lat, lon = float(lat), float(lon)
    if lat == 0.0 and lon == 0.0:
        return None
    return (lat, lon)


def _connect(dev_path: Optional[str], hostname: Optional[str]) -> Any:
    """Open a Meshtastic interface (lazy import). Raises RuntimeError on failure."""
    try:
        if hostname:
            from meshtastic.tcp_interface import TCPInterface

            return TCPInterface(hostname=hostname)
        from meshtastic.serial_interface import SerialInterface

        # devPath=None lets meshtastic auto-detect the USB node.
        return SerialInterface(devPath=dev_path)
    except ImportError as exc:
        raise RuntimeError(
            "the 'meshtastic' package is required for --meshtastic; "
            "install with `uv sync --extra meshtastic` or `pip install flockdar[meshtastic]`"
        ) from exc
    except Exception as exc:  # connection / port errors
        target = hostname or dev_path or "auto-detected serial node"
        raise RuntimeError(f"cannot connect to Meshtastic node ({target}): {exc}") from exc


class MeshtasticPositionSource:
    """Tracks the local Meshtastic node's GPS position.

    Position updates arrive over a pubsub subscription; the most recent fix
    from the *local* node (when its node number is known) wins, so other GPS-
    equipped nodes in the mesh don't move our position.
    """

    def __init__(
        self,
        *,
        dev_path: Optional[str] = None,
        hostname: Optional[str] = None,
        interface: Any = None,
        local_only: bool = True,
        subscribe: bool = True,
    ) -> None:
        self._lock = threading.Lock()
        self._pos: Optional[Position] = None
        self._my_num: Optional[int] = None
        self._local_only = local_only
        self._iface = interface if interface is not None else _connect(dev_path, hostname)
        self._seed_from_local()
        if subscribe:
            self._subscribe()

    # -- setup ----------------------------------------------------------------

    def _subscribe(self) -> None:
        from pubsub import pub  # ships with meshtastic

        pub.subscribe(self._on_receive, _POSITION_TOPIC)

    def _seed_from_local(self) -> None:
        """Record the local node number and seed an initial fix if present."""
        try:
            info = self._iface.getMyNodeInfo()
        except Exception:
            info = None
        if not info:
            return
        self._my_num = info.get("num")
        seed = position_from_packet({"decoded": {"position": info.get("position", {})}})
        if seed:
            with self._lock:
                self._pos = seed

    # -- runtime --------------------------------------------------------------

    def _on_receive(self, packet: Any = None, interface: Any = None) -> None:
        if packet is None:
            return
        if self._local_only and self._my_num is not None and packet.get("from") != self._my_num:
            return
        pos = position_from_packet(packet)
        if pos is not None:
            with self._lock:
                self._pos = pos

    def current(self) -> Optional[Position]:
        with self._lock:
            return self._pos

    def close(self) -> None:
        try:
            from pubsub import pub

            pub.unsubscribe(self._on_receive, _POSITION_TOPIC)
        except Exception:
            pass
        try:
            self._iface.close()
        except Exception:
            pass


def open_meshtastic(
    dev_path: Optional[str] = None, hostname: Optional[str] = None
) -> MeshtasticPositionSource:
    """Open a Meshtastic position source over serial (dev_path) or TCP (hostname).

    Pass neither to auto-detect a USB-connected node. Raises RuntimeError with a
    friendly message if the package is missing or the node can't be reached.
    """
    return MeshtasticPositionSource(dev_path=dev_path, hostname=hostname)
