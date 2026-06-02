# T-Deck soak-test checklist

Manual validation after firmware hardening changes. Flash the `t-deck` env and connect serial at 115200 baud.

## 1. Baseline (boot)

1. `pio run -e t-deck -t upload && pio device monitor -b 115200`
2. Type `stats` and record:
   - `free_heap` — typical 150–250 KB with UI+BLE+WiFi (varies by build)
   - `min_heap` — should equal or be close to `free_heap` at boot
   - `queue_drops` — **must be 0**

**Pass:** `queue_drops == 0`, `min_heap` within 32 KB of `free_heap`.

## 2. RF-heavy environment (30 min)

Walk or park in dense 2.4 GHz WiFi (apartment, downtown).

- On-device **Status** screen: `WiFi frames` and `BLE adverts` climb steadily; **Channel** cycles 1 → 6 → 11.
- Serial `stats`: `queue_drops` stays **≤ 5** for the whole session (brief spikes OK; sustained climb is fail).
- `min_heap` must not fall more than **16 KB** below boot baseline.

**Fail:** `queue_drops` increases by >20 in 30 min, or `min_heap` drops >32 KB.

## 3. Flock hit burst

Pass a known Flock camera (or replay `flock-*.ndjson` on a second device).

- T-Deck **Flock hits** counter increments; last-hit MAC updates.
- SD log (if enabled) contains signed `wifi`/`ble` lines.
- `queue_drops` does not increase during the burst.

**Pass:** Hits visible in UI + ingest; `queue_drops` unchanged.

## 4. SD dump stress

With a log of **3000+ lines** on the card:

1. Note `min_heap` via `stats`.
2. Run `sd dump` (or UI equivalent).
3. `stats` again after dump completes.

**Pass:** `min_heap` after dump within **4 KB** of pre-dump value (no sustained heap erosion from per-line alloc).

## 5. Long wardrive (8+ hours)

SD logging + GPS + UI enabled; device powered from battery or USB.

- Sample `stats` every 2 hours.
- **Pass:** `min_heap` plateaus (drift **< 10 KB/hour** after first hour). If erosion exceeds 10 KB/hour, capture serial log and file an issue (likely NimBLE/TFT library heap).
- **Pass:** `queue_drops` total **< 0.1%** of `emits` (e.g. <10 drops per 10k detections).

## 6. GPS / Nearby RF stability

Acquire GPS fix; open **Nearby RF** list.

- Entries with GPS show stable coordinates (no jumping lat/lon between refreshes while standing still).
- **Pass:** Coordinates stable within ~10 m; no torn/garbage values.

## Serial commands

| Command | Purpose |
|---------|---------|
| `stats` | JSON counters: `queue_drops`, `emits`, `wifi_mgmt`, `ble_adverts`, `rf_events`, `free_heap`, `min_heap` |
| `sd list` / `sd dump` | SD log management |
| `tz -300` | US Eastern local time offset (minutes) |
