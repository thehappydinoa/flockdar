#!/usr/bin/env python3
"""
Flock Safety device detector — Textual TUI.

Usage:
    uv run flockdar <wigle.sqlite>
    uv run flockdar <WigleWifi_export.csv.gz>
    uv run flockdar <flock-0001.ndjson>          # ESP32 SD-card log
    uv run flockdar --serial /dev/ttyUSB0        # live ESP32 capture

Keybindings:
    m  Open selected location in Google Maps
    v  Open selected location in Google Street View
    c  Copy MAC address(es) to clipboard
    n  Enrich visible hits (OSM/DeFlock, ALPRWatch, WiGLE if configured)
    d  Discover unseen Flock devices from WiGLE (requires WiGLE key)
    w  Configure WiGLE API credentials
    e  Export visible hits to CSV
    k  Export visible hits to KML
    g  Export visible hits to GeoJSON
    o  Open iD editor + copy OSM tags for selected camera
    r  Reload / re-scan file
    q  Quit
"""

import argparse
import contextlib
import sys
import webbrowser
from pathlib import Path

from textual import on, work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical, VerticalScroll
from textual.css.query import NoMatches
from textual.reactive import reactive
from textual.screen import ModalScreen
from textual.widgets import (
    Button,
    Checkbox,
    DataTable,
    Footer,
    Header,
    Input,
    Label,
    LoadingIndicator,
    Rule,
    Static,
)

from . import detect, serial_import
from . import enrich as enrich_mod
from .detect import Cluster, Hit
from .discover import DISCOVERED_SIGNAL, build_discovery, cache_age_seconds
from .enrich import (
    ENRICHMENT_SIGNAL_LABELS,
    build_enrichers,
    enrich_hits_async,
    load_config,
    save_config,
)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

CONF_COLOR = {3: "bold red", 2: "yellow", 1: "dim cyan"}

# OSM tags for a confirmed Flock Safety ALPR camera node
_OSM_TAG_TEMPLATE = """\
man_made=surveillance
surveillance:type=ALPR
surveillance=outdoor
camera:type=fixed
operator=Flock Safety
surveillance:zone=traffic"""


def _osm_tags(cluster: "Cluster") -> str:
    """Return OSM tag block for the cluster, with brand if confidence is HIGH."""
    base = _OSM_TAG_TEMPLATE
    if cluster.confidence == 3:
        base += "\nbrand=Flock Safety\nbrand:wikidata=Q113557215"
    return base


TYPE_ICON = {"W": "📶", "WIFI": "📶", "E": "🔵", "B": "🔵", "BLE": "🔵", "BT": "🔵"}


def _type_icon(t: str) -> str:
    return TYPE_ICON.get(t.upper(), "❓")


def _cluster_icon(c: Cluster) -> str:
    icons = {_type_icon(h.device_type) for h in c.hits}
    # Mark WiGLE-discovered hits with a globe prefix
    all_signals = {label for h in c.hits for label, _ in h.signals}
    prefix = "🌐" if DISCOVERED_SIGNAL in all_signals else ""
    return prefix + "".join(sorted(icons))


def _is_ble(t: str) -> bool:
    return t.upper() in ("E", "B", "BLE", "BT")


def _is_wifi(t: str) -> bool:
    return t.upper() in ("W", "WIFI")


# ---------------------------------------------------------------------------
# Widgets
# ---------------------------------------------------------------------------


class DetailPanel(VerticalScroll):
    DEFAULT_CSS = """
    DetailPanel {
        width: 1fr;
        border: solid $primary-darken-2;
        padding: 1 2;
        background: $surface-darken-1;
    }
    DetailPanel Label { width: 100%; }
    DetailPanel .detail-key   { color: $text-muted; width: 14; }
    DetailPanel .detail-val   { color: $text; }
    DetailPanel .detail-sub   { color: $text-muted; text-style: bold; margin-top: 1; }
    DetailPanel .detail-head  { text-style: bold; color: $primary; margin-bottom: 1; }
    DetailPanel .detail-sig   { color: $warning; text-style: italic; }
    DetailPanel .detail-svc   { color: $text-muted; text-style: italic; }
    DetailPanel .detail-hint  { color: $success; text-style: italic; }
    """

    def compose(self) -> ComposeResult:
        yield Label("Select a device", classes="detail-head")

    def show(self, cluster: Cluster) -> None:
        self.remove_children()
        conf_color = CONF_COLOR[cluster.confidence]
        n = len(cluster.hits)

        self.mount(
            Label(
                f"[{conf_color}]{cluster.confidence_label}[/{conf_color}]  "
                f"{_cluster_icon(cluster)}  "
                + (f"{n} devices" if n > 1 else cluster.hits[0].device_type),
                classes="detail-head",
            )
        )
        self.mount(Rule())

        if cluster.lat or cluster.lon:
            self.mount(
                Horizontal(
                    Label("Location:", classes="detail-key"),
                    Label(f"{cluster.lat:.6f}, {cluster.lon:.6f}", classes="detail-val"),
                )
            )
            hint = "  [m] Maps   [v] Street View"
            if cluster.confidence >= 2:
                hint += "   [o] Add to OSM"
            self.mount(Label(hint, classes="detail-hint"))

        if n == 1:
            self._show_single(cluster.hits[0])
        else:
            self._show_cluster(cluster)

        if cluster.confidence >= 2 and (cluster.lat or cluster.lon):
            self.show_osm_tags(cluster)

    def _show_single(self, h: Hit) -> None:
        rows = [
            ("MAC", h.mac),
            ("Name", h.ssid or "(hidden)"),
            ("RSSI", f"{h.rssi} dBm"),
            ("Freq", f"{h.frequency} MHz" if h.frequency else "—"),
            ("Auth", h.capabilities or "—"),
            ("MfgrId", str(h.mfgrid) if h.mfgrid else "—"),
            ("Seen", h.first_seen or "—"),
        ]
        for key, val in rows:
            self.mount(
                Horizontal(
                    Label(f"{key}:", classes="detail-key"),
                    Label(str(val), classes="detail-val"),
                )
            )
        self.mount(Rule())
        self.mount(Label("Signals", classes="detail-key"))
        for label, detail in h.signals:
            txt = f"  • {label}"
            if detail:
                txt += f"  [dim]{detail[:60]}[/dim]"
            self.mount(Label(txt, classes="detail-sig"))
        if h.services:
            self.mount(Rule())
            self.mount(Label("BLE Services", classes="detail-key"))
            for svc in h.services.split():
                self.mount(Label(f"  {svc}", classes="detail-svc"))

    def show_osm_tags(self, cluster: "Cluster") -> None:
        """Append OSM tag block to the current detail view."""
        self.mount(Rule())
        self.mount(Label("OSM tags  ([o] to open iD + copy)", classes="detail-key"))
        for line in _osm_tags(cluster).splitlines():
            self.mount(Label(f"  {line}", classes="detail-svc"))

    def _show_cluster(self, cluster: Cluster) -> None:
        self.mount(Rule())
        for h in cluster.hits:
            conf_color = CONF_COLOR[h.confidence]
            self.mount(
                Label(
                    f"[{conf_color}]{'─' * 3} {_type_icon(h.device_type)} {h.mac}[/{conf_color}]"
                    + (f"  {h.ssid!r}" if h.ssid else ""),
                    classes="detail-sub",
                )
            )
            for label, detail in h.signals:
                txt = f"    • {label}"
                if detail:
                    txt += f"  [dim]{detail[:50]}[/dim]"
                self.mount(Label(txt, classes="detail-sig"))


class FilterPanel(Vertical):
    DEFAULT_CSS = """
    FilterPanel {
        width: 20;
        border: solid $primary-darken-2;
        padding: 1 1;
        background: $surface-darken-1;
    }
    FilterPanel Label    { color: $text-muted; text-style: bold; margin-bottom: 0; }
    FilterPanel Checkbox { margin: 0; padding: 0; background: transparent; }
    FilterPanel Rule     { margin: 1 0; }
    """

    def compose(self) -> ComposeResult:
        yield Label("Confidence")
        yield Checkbox("HIGH", value=True, id="cf_high")
        yield Checkbox("MEDIUM", value=True, id="cf_med")
        yield Checkbox("LOW", value=False, id="cf_low")
        yield Rule()
        yield Label("Type")
        yield Checkbox("WiFi", value=True, id="t_wifi")
        yield Checkbox("BLE / BT", value=True, id="t_ble")
        yield Checkbox("Other", value=True, id="t_other")
        yield Rule()
        yield Label("Display")
        yield Checkbox("Group nearby", value=False, id="group_nearby")

    def active_confidences(self) -> set[int]:
        result = set()
        if self.query_one("#cf_high", Checkbox).value:
            result.add(3)
        if self.query_one("#cf_med", Checkbox).value:
            result.add(2)
        if self.query_one("#cf_low", Checkbox).value:
            result.add(1)
        return result

    def show_wifi(self) -> bool:
        return self.query_one("#t_wifi", Checkbox).value

    def show_ble(self) -> bool:
        return self.query_one("#t_ble", Checkbox).value

    def show_other(self) -> bool:
        return self.query_one("#t_other", Checkbox).value

    def group_nearby(self) -> bool:
        return self.query_one("#group_nearby", Checkbox).value


# ---------------------------------------------------------------------------
# WiGLE config modal
# ---------------------------------------------------------------------------


class WiGLEConfigScreen(ModalScreen):
    DEFAULT_CSS = """
    WiGLEConfigScreen {
        align: center middle;
    }
    #wigle-dialog {
        width: 60;
        height: auto;
        border: solid $primary;
        background: $surface;
        padding: 1 2;
    }
    #wigle-dialog Label { margin-bottom: 0; color: $text-muted; }
    #wigle-dialog Input { margin-bottom: 1; }
    #wigle-dialog .hint { color: $text-muted; text-style: italic; margin-bottom: 1; }
    #wigle-buttons { layout: horizontal; align: right middle; height: 3; }
    #wigle-buttons Button { margin-left: 1; }
    """

    def compose(self) -> ComposeResult:
        cfg = load_config()
        with Vertical(id="wigle-dialog"):
            yield Label("WiGLE API credentials", classes="detail-head")
            yield Static(
                "Get your API name + token at wigle.net → My Account → API",
                classes="hint",
            )
            yield Label("API Name")
            yield Input(
                value=cfg.get("wigle_api_name", ""), id="wigle-name", placeholder="your_api_name"
            )
            yield Label("API Token")
            yield Input(
                value=cfg.get("wigle_api_token", ""),
                id="wigle-token",
                placeholder="your_api_token",
                password=True,
            )
            with Horizontal(id="wigle-buttons"):
                yield Button("Cancel", variant="default", id="wigle-cancel")
                yield Button("Save", variant="primary", id="wigle-save")

    @on(Button.Pressed, "#wigle-cancel")
    def _cancel(self) -> None:
        self.dismiss(None)

    @on(Button.Pressed, "#wigle-save")
    def _save(self) -> None:
        name = self.query_one("#wigle-name", Input).value.strip()
        token = self.query_one("#wigle-token", Input).value.strip()
        if name and token:
            cfg = load_config()
            cfg["wigle_api_name"] = name
            cfg["wigle_api_token"] = token
            save_config(cfg)
        self.dismiss((name, token))


# ---------------------------------------------------------------------------
# Main app
# ---------------------------------------------------------------------------


class FlockDetectApp(App):
    TITLE = "Flock Safety Detector"
    SUB_TITLE = ""

    CSS = """
    Screen { layout: vertical; }

    #main-row {
        layout: horizontal;
        height: 1fr;
    }
    #right-col {
        layout: vertical;
        width: 1fr;
    }
    DataTable {
        height: 1fr;
        border: solid $primary-darken-2;
    }
    DetailPanel {
        height: 1fr;
        min-height: 14;
        max-height: 24;
    }
    #status-bar {
        height: 1;
        background: $primary-darken-3;
        color: $text-muted;
        padding: 0 1;
    }
    LoadingIndicator { height: 1fr; }
    """

    BINDINGS = [
        Binding("m", "open_maps", "Maps"),
        Binding("v", "open_streetview", "Street View"),
        Binding("c", "copy_mac", "Copy MAC"),
        Binding("n", "enrich", "Enrich"),
        Binding("d", "discover", "Discover"),
        Binding("D", "discover_force", "Discover (refresh)", show=False),
        Binding("w", "wigle_config", "WiGLE Key"),
        Binding("e", "export_csv", "Export CSV"),
        Binding("k", "export_kml", "Export KML"),
        Binding("g", "export_geojson", "Export GeoJSON"),
        Binding("o", "open_osm", "Add to OSM"),
        Binding("r", "reload", "Reload"),
        Binding("q", "quit", "Quit"),
    ]

    all_hits: reactive[list[Hit]] = reactive([], recompose=False)
    total_records: reactive[int] = reactive(0)

    def __init__(
        self,
        input_path: Path | None = None,
        *,
        serial_port: str | None = None,
        baud: int = serial_import.DEFAULT_BAUD,
        hmac_key: str | None = None,
    ) -> None:
        super().__init__()
        self.input_path = input_path
        self.serial_port = serial_port
        self.baud = baud
        self._hmac_key = serial_import.resolve_key(hmac_key)
        self._display_items: list[Cluster] = []
        self._enriching = False
        self._discovering = False
        # Live-mode state: dedup-by-MAC map populated as detections arrive.
        self._live_seen: dict[str, Hit] = {}
        self._serial_stop = False
        self._enrich_cache = enrich_mod.load_enrich_cache()

    @property
    def _out_stem(self) -> str:
        """Filename stem for exports (handles live serial mode)."""
        return self.input_path.stem if self.input_path else "flock_serial"

    # ------------------------------------------------------------------
    # Layout
    # ------------------------------------------------------------------

    def compose(self) -> ComposeResult:
        yield Header()
        yield Static("", id="status-bar")
        with Horizontal(id="main-row"):
            yield FilterPanel(id="filters")
            with Vertical(id="right-col"):
                yield LoadingIndicator()
        yield Footer()

    def on_mount(self) -> None:
        if self.serial_port:
            self.sub_title = f"live: {self.serial_port}"
            self._load_serial()
        else:
            self.sub_title = self.input_path.name
            self._load_data()

    def on_unmount(self) -> None:
        # Let the serial reader thread exit (it polls this between reads).
        self._serial_stop = True

    # ------------------------------------------------------------------
    # Data loading
    # ------------------------------------------------------------------

    @work(thread=True)
    def _load_data(self) -> None:
        suffix = self.input_path.suffix.lower()
        if suffix in (".ndjson", ".jsonl", ".log"):
            hits, total = serial_import.load_log(self.input_path, self._hmac_key)
        else:
            hits, total = detect.run_detection(self.input_path)
        self.call_from_thread(self._on_data_loaded, hits, total)

    def _build_table(self) -> None:
        """Mount the results table + detail panel (shared by file & live modes)."""
        right = self.query_one("#right-col")
        right.remove_children()
        right.mount(DataTable(id="device-table", cursor_type="row"))
        right.mount(DetailPanel(id="detail"))

        table = self.query_one("#device-table", DataTable)
        table.add_columns(
            "", "MAC / Devices", "Name", "Conf", "Type", "RSSI", "Lat", "Lon", "Date", "Enriched"
        )
        table.focus()

    def _on_data_loaded(self, hits: list[Hit], total: int) -> None:
        enrich_mod.apply_cached_enrichment(hits, self._enrich_cache)
        self.all_hits = hits
        self.total_records = total
        self._build_table()
        self._rebuild_table()

    # ------------------------------------------------------------------
    # Live serial ingestion
    # ------------------------------------------------------------------

    @work(thread=True)
    def _load_serial(self) -> None:
        self.call_from_thread(self._build_table)
        try:
            lines = serial_import.serial_lines(
                self.serial_port, self.baud, should_stop=lambda: self._serial_stop
            )
            for hit in serial_import.iter_hits(lines, self._hmac_key):
                if self._serial_stop:
                    break
                self.call_from_thread(self._on_live_hit, hit)
        except RuntimeError as exc:  # pyserial missing / port error
            self.call_from_thread(self.notify, str(exc), severity="error", title="Serial")

    def _on_live_hit(self, hit: Hit) -> None:
        is_new = serial_import.merge_hit(self._live_seen, hit)
        self.total_records += 1
        self.all_hits = sorted(self._live_seen.values(), key=lambda h: (-h.confidence, h.mac))
        self._rebuild_table()
        if is_new and hit.confidence == 3:
            self.bell()
            self.notify(
                f"{hit.device_type} {hit.mac}\n{hit.signals_str()}",
                title="⚠ HIGH-confidence Flock device",
                severity="warning",
            )

    # ------------------------------------------------------------------
    # Table population
    # ------------------------------------------------------------------

    def _rebuild_table(self) -> None:
        try:
            table = self.query_one("#device-table", DataTable)
        except NoMatches:
            return

        fp = self.query_one("#filters", FilterPanel)
        active_conf = fp.active_confidences()

        def visible(h: Hit) -> bool:
            if h.confidence not in active_conf:
                return False
            t = h.device_type.upper()
            if _is_wifi(t) and not fp.show_wifi():
                return False
            if _is_ble(t) and not fp.show_ble():
                return False
            return _is_wifi(t) or _is_ble(t) or fp.show_other()

        filtered_hits = [h for h in self.all_hits if visible(h)]

        if fp.group_nearby():
            self._display_items = detect.cluster_hits(filtered_hits)
        else:
            self._display_items = detect.single_clusters(filtered_hits)

        table.clear()
        for c in self._display_items:
            conf_color = CONF_COLOR[c.confidence]
            n = len(c.hits)
            mac_col = c.hits[0].mac if n == 1 else f"[dim]{n} devices[/dim]"
            table.add_row(
                _cluster_icon(c),
                mac_col,
                c.label or "[dim](hidden)[/dim]",
                f"[{conf_color}]{c.confidence_label}[/{conf_color}]",
                c.types,
                f"{c.best_rssi}",
                f"{c.lat:.4f}" if (c.lat or c.lon) else "—",
                f"{c.lon:.4f}" if (c.lat or c.lon) else "—",
                (c.first_seen or "")[:10] or "—",
                c.enrichment_label(ENRICHMENT_SIGNAL_LABELS),
            )

        n_items = len(self._display_items)
        n_hits = sum(len(c.hits) for c in self._display_items)
        total = len(self.all_hits)
        label = "clusters" if fp.group_nearby() else "devices"
        self.query_one("#status-bar", Static).update(
            f" {self.total_records:,} records scanned  •  "
            f"{total} matched  •  "
            f"{n_items} {label} shown"
            + (f" ({n_hits} devices)" if fp.group_nearby() else "")
            + "  •  [m] Maps  [v] Street View  [c] Copy MAC  [e] CSV  [k] KML"
        )

    # ------------------------------------------------------------------
    # Events
    # ------------------------------------------------------------------

    @on(Checkbox.Changed)
    def _filter_changed(self, _: Checkbox.Changed) -> None:
        self._rebuild_table()

    @on(DataTable.RowHighlighted, "#device-table")
    def _row_highlighted(self, event: DataTable.RowHighlighted) -> None:
        try:
            detail = self.query_one("#detail", DetailPanel)
        except NoMatches:
            return
        idx = event.cursor_row
        if 0 <= idx < len(self._display_items):
            detail.show(self._display_items[idx])

    # ------------------------------------------------------------------
    # Selection helper
    # ------------------------------------------------------------------

    def _selected(self) -> Cluster | None:
        try:
            table = self.query_one("#device-table", DataTable)
        except NoMatches:
            return None
        idx = table.cursor_row
        if 0 <= idx < len(self._display_items):
            return self._display_items[idx]
        return None

    # ------------------------------------------------------------------
    # Actions
    # ------------------------------------------------------------------

    def action_open_maps(self) -> None:
        c = self._selected()
        if c and (c.lat or c.lon):
            webbrowser.open(c.maps_url)
        else:
            self.notify("No location data for this device.", severity="warning")

    def action_open_streetview(self) -> None:
        c = self._selected()
        if c and (c.lat or c.lon):
            webbrowser.open(c.streetview_url)
        else:
            self.notify("No location data for this device.", severity="warning")

    def action_copy_mac(self) -> None:
        c = self._selected()
        if not c:
            return
        text = c.mac_list
        self.copy_to_clipboard(text)
        n = len(c.hits)
        self.notify(
            f"Copied {n} MAC{'s' if n > 1 else ''} to clipboard.",
            title="Copied",
        )

    def action_export_csv(self) -> None:
        hits = [h for c in self._display_items for h in c.hits]
        if not hits:
            return
        out = self._out_stem + "_flock_hits.csv"
        detect.export_csv(hits, out)
        self.notify(f"Exported {len(hits)} rows → {out}", title="CSV saved")

    def action_export_kml(self) -> None:
        hits = [h for c in self._display_items for h in c.hits]
        if not hits:
            return
        out = self._out_stem + "_flock_hits.kml"
        detect.export_kml(hits, out)
        self.notify(f"Exported {len(hits)} placemarks → {out}", title="KML saved")

    def action_export_geojson(self) -> None:
        hits = [h for c in self._display_items for h in c.hits]
        if not hits:
            return
        out = self._out_stem + "_flock_hits.geojson"
        detect.export_geojson(hits, out)
        self.notify(f"Exported {len(hits)} features → {out}", title="GeoJSON saved")

    def action_open_osm(self) -> None:
        c = self._selected()
        if not c or not (c.lat or c.lon):
            self.notify("No location data for this device.", severity="warning")
            return
        # Open iD editor centred on the camera location at zoom 19
        url = f"https://www.openstreetmap.org/edit?editor=id#map=19/{c.lat}/{c.lon}"
        webbrowser.open(url)
        # Copy OSM tags to clipboard so user can paste them in iD
        tags = _osm_tags(c)
        self.copy_to_clipboard(tags)
        self.notify(
            "Opened iD editor — OSM tags copied to clipboard.\n"
            "Add a node at the camera pole and paste the tags.",
            title="OSM contribution",
        )

    def action_reload(self) -> None:
        if self.serial_port:
            self.notify("Live serial mode streams continuously — nothing to reload.")
            return
        try:
            right = self.query_one("#right-col")
            right.remove_children()
            right.mount(LoadingIndicator())
        except NoMatches:
            pass
        self._load_data()

    def action_enrich(self) -> None:
        if self._enriching:
            self.notify("Enrichment already running.", severity="warning")
            return
        hits = [h for c in self._display_items for h in c.hits]
        if not hits:
            self.notify("No hits to enrich.", severity="warning")
            return
        self._enriching = True
        self.notify(
            f"Enriching {len(hits)} device(s) via OSM/DeFlock + ALPRWatch…",
            title="Enrichment started",
        )
        self._run_enrichment(hits)

    @work
    async def _run_enrichment(self, hits: list[Hit]) -> None:
        enrichers = build_enrichers(cache=self._enrich_cache)
        enriched = 0

        def on_hit_done(hit: Hit) -> None:
            nonlocal enriched
            enriched += 1
            self._on_hit_enriched(hit, enriched, len(hits))

        await enrich_hits_async(hits, enrichers, callback=on_hit_done, cache=self._enrich_cache)
        self._on_enrichment_done(enriched)

    def _on_hit_enriched(self, hit: Hit, done: int, total: int) -> None:
        try:
            table = self.query_one("#device-table", DataTable)
            detail = self.query_one("#detail", DetailPanel)
            # Find which display row this hit belongs to and update its Enriched cell
            for row_idx, cluster in enumerate(self._display_items):
                if hit in cluster.hits:
                    enrich_col = 9  # "Enriched" is column index 9
                    table.update_cell_at(
                        (row_idx, enrich_col),
                        cluster.enrichment_label(ENRICHMENT_SIGNAL_LABELS),
                        update_width=False,
                    )
                    if row_idx == table.cursor_row:
                        detail.show(cluster)
                    break
            self.query_one("#status-bar", Static).update(
                f" Enriching… {done}/{total}  (press [n] to re-run when done)"
            )
        except (NoMatches, Exception):
            pass

    def _on_enrichment_done(self, enriched: int) -> None:
        self._enriching = False
        self._rebuild_table()
        self.notify(
            f"Enrichment complete — {enriched} device(s) processed.",
            title="Done",
        )

    def action_wigle_config(self) -> None:
        def on_dismiss(result) -> None:
            if result:
                name, token = result
                if name and token:
                    self.notify("WiGLE credentials saved.", title="WiGLE")

        self.push_screen(WiGLEConfigScreen(), on_dismiss)

    def action_discover(self) -> None:
        if self._discovering:
            self.notify("Discovery already running.", severity="warning")
            return
        disc = build_discovery()
        if disc is None:
            self.notify(
                "No WiGLE credentials — press [w] to configure.",
                title="Discover",
                severity="warning",
            )
            return

        # Check cache and inform user
        age = cache_age_seconds()
        if age is not None:
            hours = age / 3600
            self.notify(
                f"Using cached results ({hours:.1f} h old).  Hold Shift+D to force refresh.",
                title="Discovery (cached)",
            )
        else:
            self.notify(
                "Fetching from WiGLE — results will be cached for 24 h.",
                title="Discovery started",
            )

        self._discovering = True
        self._run_discovery(disc, force_refresh=False)

    def action_discover_force(self) -> None:
        """Force-refresh discovery, ignoring the cache."""
        if self._discovering:
            self.notify("Discovery already running.", severity="warning")
            return
        disc = build_discovery()
        if disc is None:
            self.notify(
                "No WiGLE credentials — press [w] to configure.",
                title="Discover",
                severity="warning",
            )
            return
        self._discovering = True
        self.notify("Force-refreshing from WiGLE (ignoring cache)…", title="Discovery started")
        self._run_discovery(disc, force_refresh=True)

    @work
    async def _run_discovery(self, disc, force_refresh: bool = False) -> None:
        def on_progress(raw: int, total: int, label: str) -> None:
            with contextlib.suppress(NoMatches):
                self.query_one("#status-bar", Static).update(
                    f" Discovering… {raw}/{total} rows from WiGLE ({label})"
                )

        new_hits, stats = await disc.discover(
            force_refresh=force_refresh,
            progress=on_progress,
        )

        # Merge into all_hits
        existing = {h.mac.lower(): h for h in self.all_hits}
        added = 0
        for h in new_hits:
            mac = h.mac.lower()
            if mac in existing:
                for s in h.signals:
                    existing[mac].add_signal(*s)
            else:
                existing[mac] = h
                added += 1

        self.all_hits = sorted(existing.values(), key=lambda h: (-h.confidence, h.mac))
        self._discovering = False
        self._rebuild_table()

        if stats.from_cache:
            age_h = stats.cache_age_s / 3600
            self.notify(
                f"Loaded {stats.hits_converted:,} devices from cache "
                f"({age_h:.1f} h old) — {added} new to your data.",
                title="Discovery complete",
            )
        else:
            msg = (
                f"Fetched {stats.raw_fetched:,} rows → {stats.hits_converted:,} devices "
                f"({stats.total_available:,} total in WiGLE) — {added} new to your data."
            )
            if stats.error:
                msg += f"  ⚠ {stats.error}"
            self.notify(
                msg,
                title="Discovery complete",
                severity="warning" if stats.error else "information",
            )


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Detect Flock Safety devices in WiGLE data exports."
    )
    parser.add_argument(
        "input",
        nargs="?",
        help="WiGLE SQLite DB (.sqlite), CSV export (.csv.gz), or "
        "flockdar-esp32 NDJSON log (.ndjson/.jsonl/.log)",
    )
    parser.add_argument(
        "--serial",
        metavar="PORT",
        help="live ingest from a flockdar-esp32 device (e.g. /dev/ttyUSB0, COM3)",
    )
    parser.add_argument(
        "--baud", type=int, default=serial_import.DEFAULT_BAUD, help="serial baud rate"
    )
    parser.add_argument(
        "--key",
        help=f"HMAC key for live frames (or ${serial_import.ENV_HMAC_KEY}; "
        "default matches the firmware default)",
    )
    args = parser.parse_args()

    if args.serial:
        FlockDetectApp(serial_port=args.serial, baud=args.baud, hmac_key=args.key).run()
        return

    if not args.input:
        parser.error("provide an input file or --serial PORT")

    path = Path(args.input)
    if not path.exists():
        sys.exit(f"File not found: {path}")

    FlockDetectApp(path, hmac_key=args.key).run()


if __name__ == "__main__":
    main()
