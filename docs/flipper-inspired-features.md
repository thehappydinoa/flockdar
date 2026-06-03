# Flipper Zero–inspired features for the flockdar T-Deck

A design exploration: which Flipper Zero interface and feature ideas are worth
bringing to the flockdar T-Deck firmware, ranked by value vs. effort and
honest about what the hardware can and can't do.

> Status: **proposal / discussion.** No code yet — this is the menu of options
> to pick from. Nothing here is built.

---

## What we already have (so we don't reinvent it)

The T-Deck UI (`esp32/src/tdeck_ui*.cpp`) already does a lot the Flipper is
loved for:

- Multi-screen UI — Status / List / Detail / Help / Nearby, carousel pages,
  themed chrome + icons (`tdeck_theme.h`, `tdeck_icons.cpp`).
- Live device list with dedup (`rf_sightings`, `flock_dedup`) and detail view.
- On-device stats/heap telemetry (`stats.h`), SD logging with `dump`/`list`,
  serial commands, screenshot-over-USB.
- GPS, signed JSON output, confidence tiers mirrored from `detect.py`.

So the gaps are mostly about **interaction feel**, **discoverability**, and a
few **signature/capture** features the Flipper popularized.

---

## The Flipper "feel" — what actually makes it good

1. **A real main menu.** Everything is one scrollable, icon-led list you drive
   with the D-pad. You always know where you are and what you can do.
2. **Instant tactile feedback.** Scroll ticks, selection beeps, a vibration/LED
   on an event. You feel the device respond.
3. **Saved "captures" you can browse and replay.** NFC/Sub-GHz/IR saves are
   first-class files you name, list, and re-open on the device.
4. **A clear status/idle screen** (the dolphin) that doubles as a heartbeat.
5. **Everything is a named app** with consistent back/OK semantics.

The T-Deck has a **trackball + keyboard + speaker + bigger color screen + LoRa**
— in several ways *more* than a Flipper. The opportunity is to borrow the
*structure and feedback*, not to copy screens 1:1.

---

## Proposed features, ranked

### Tier 1 — high value, modest effort (recommended first)

| # | Feature | Flipper analog | Why it fits flockdar |
|---|---------|----------------|----------------------|
| 1 | **Main menu / app launcher** | Main menu | A single trackball-driven, icon-led menu routing to Scan / Sightings / Map / Saved / Settings / About. Unifies today's carousel + screens into one mental model and makes every feature discoverable. |
| 2 | **Audio + haptic/LED alert cues** | Beeps / vibro | Speaker beep + screen flash (and LED if wired) on a new HIGH-confidence Flock hit. Distinct tones per confidence. Driving = eyes-up alerting. Build-flag + UI toggle. |
| 3 | **Saved capture sessions browser** | Saved files | Browse SD `flock-*.ndjson` logs on-device: list, per-file summary (count, first/last time, top hits), and stats. We already write the logs and have `list`/`dump`. |
| 4 | **"Detector" signal-strength screen** | Sub-GHz/NFC read | A focused proximity screen: pick a tracked MAC, show a live RSSI bar + tone that rises as you approach. Genuinely useful for physically locating a camera. |

### Tier 2 — high value, more effort

| # | Feature | Flipper analog | Why it fits flockdar |
|---|---------|----------------|----------------------|
| 5 | **On-device mini-map** | (GPS apps) | Plot recent hits relative to current GPS position (heading-up dots, range rings). Color by confidence. No tiles — vector dots only, cheap to draw. |
| 6 | **Settings screen w/ persistence** | Settings | On-device toggles saved to NVS/SD: sound on/off, channel lock, confidence floor for alerts, screen brightness, units. Today these are compile-time. |
| 7 | **Quick stats / "trip" dashboard** | — | Session totals: unique cameras, per-confidence counts, distance travelled (from GPS), uptime — a wardriving scoreboard. |

### Tier 3 — fun / stretch (clearly Flipper-flavored, lower priority)

| # | Feature | Flipper analog | Notes |
|---|---------|----------------|-------|
| 8 | **Mascot / idle animation** | Dolphin | A small flock-bird idle screen that reacts to activity (more hits → livelier). Pure personality; cheap with the existing draw layer. |
| 9 | **"Achievements" / level** | Dolphin levels | Lightweight milestones (10/100/1000 cameras). Gamifies long drives. |
| 10 | **Boot splash + sound** | Boot animation | Branded splash + chirp on power-up. Trivial, high polish-per-line. |

### Explicitly out of scope (and why)

- **Sub-GHz / IR / NFC / BadUSB cloning.** The Flipper has dedicated radios
  (CC1101, 125 kHz/13.56 MHz, IR LED) the T-Deck simply doesn't have. flockdar
  is a **passive detector**; transmit/clone features are both infeasible here
  and off-mission. (LoRa-over-Meshtastic alerting is tracked separately.)
- **Generic "scan everything" toy modes.** We stay focused on Flock/ALPR
  detection; the Nearby list already covers ambient RF awareness.

---

## Suggested first slice

If we do one thing: **#1 main menu + #2 alert cues** together. The menu gives
the whole device a Flipper-like spine that every later feature slots into, and
audio/haptic cues are the single biggest "feels like a Flipper" win for the
least code. **#3 (saved sessions)** is the natural follow-up since the data and
SD plumbing already exist.

## Open questions for you

- Is there a **specific Flipper screen/interaction** you want replicated, or is
  "the general feel" the goal?
- **Hardware:** is an external LED/buzzer in play, or speaker-only for cues?
- Priority: **navigation polish** (menu/settings) vs. a **headline feature**
  (detector screen, mini-map) first?
