# T-Deck soak-test checklist

Manual validation after firmware hardening changes. Flash the `t-deck` env and connect serial at 115200 baud.

## 1. Baseline (boot)

1. `pio run -e t-deck -t upload && pio device monitor -b 115200`
2. Type `stats` and record:
   - `ram` — e.g. `"0.18 MB (68%)"` heap used (varies by build)
   - `queue_drops` — **must be 0**

**Pass:** `queue_drops == 0`, `heap_used_pct` typically 50–80% at boot.

## 2. RF-heavy environment (30 min)

Walk or park in dense 2.4 GHz WiFi (apartment, downtown).

- On-device **Status** screen: `WiFi frames` and `BLE adverts` climb steadily; **Channel** cycles 1 → 6 → 11.
- Serial `stats`: `queue_drops` stays **≤ 5** for the whole session (brief spikes OK; sustained climb is fail).
- `heap_used_pct` should stay within ~10 points of boot unless many new features load.

**Fail:** `queue_drops` increases by >20 in 30 min, or `heap_used_pct` climbs steadily (>15 points in 30 min).

## 3. Flock hit burst

Pass a known Flock camera (or replay `flock-*.ndjson` on a second device).

- T-Deck **Flock hits** counter increments; last-hit MAC updates.
- SD log (if enabled) contains signed `wifi`/`ble` lines.
- `queue_drops` does not increase during the burst.

**Pass:** Hits visible in UI + ingest; `queue_drops` unchanged.

## 4. SD dump stress

With a log of **3000+ lines** on the card:

1. Note `ram` / `heap_used_pct` via `stats`.
2. Run `sd dump` (or UI equivalent).
3. `stats` again after dump completes.

**Pass:** `heap_used_pct` after dump within **5 points** of pre-dump value.

## 5. Long wardrive (8+ hours)

SD logging + GPS + UI enabled; device powered from battery or USB.

- Sample `stats` every 2 hours.
- **Pass:** `heap_used_pct` plateaus (drift **< 5 points/hour** after first hour). If it keeps climbing, capture serial log and file an issue (likely NimBLE/TFT library heap).
- **Pass:** `queue_drops` total **< 0.1%** of `emits` (e.g. <10 drops per 10k detections).

## 6. GPS / Nearby RF stability

Acquire GPS fix; open **Nearby RF** list.

- Entries with GPS show stable coordinates (no jumping lat/lon between refreshes while standing still).
- **Pass:** Coordinates stable within ~10 m; no torn/garbage values.

## Serial commands

| Command | Purpose |
|---------|---------|
| `stats` | JSON counters: `queue_drops`, `emits`, `wifi_mgmt`, `ble_adverts`, `rf_events`, `heap_used_pct`, `ram` |
| `sd list` / `sd dump` | SD log management |
| `tz -300` | US Eastern local time offset (minutes) |
