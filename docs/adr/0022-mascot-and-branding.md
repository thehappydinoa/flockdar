# ADR-0022: Raven as project mascot

**Status:** Accepted  
**Date:** 2026-06-04

## Decision

**A raven** is the project mascot.

## Rationale

Flock Safety's ALPR camera hardware line is called "Raven." The project mascot watching Flock's Ravens is itself a Raven — intentional, ironic, and accurate. Ravens are also:

- Highly intelligent, associated with observation and memory
- Silent and patient — consistent with passive RF detection
- Dark, corvid, and associated with Edgar Allan Poe's "Nevermore" — appropriate energy for a counter-surveillance tool
- Collectors — ravens hoard shiny objects the way this project hoards MAC addresses

## T-Deck display

The raven replaces generic emoji mood faces on the T-Deck display. ASCII/pixel art raven that shifts posture with mood state:

```
Bored (idle):          Alert (hit detected):   Excited (cluster):
    ,_,                      >v<                    \>V</
   (o,o)                    (O,O)                   (O,O)
   /)  )                    /)  )                   /)  )
───"─"──                  ─"───"                  ─"───"─
```

"I remember you" (repeat MAC): raven tilts head.  
Sleepy (low battery): raven with drooping eyes.  
Synced via LoRa: raven spreads wings briefly.

## Naming

The mascot raven is named **"Poe"** — after Edgar Allan Poe, author of *The Raven*. Poe appears in:

- Boot screen: `Poe is watching...`
- Run complete: `Poe logged 12 hits this run`
- New hit: `Poe spotted something`
- Low battery: `Poe is tired`
- LoRa sync confirmed: `Poe reported in`

## Auto-generated run names

Run names follow the pattern `[Adjective] [Raven trait] #N`:

```
Silent Vigil #1
Midnight Circuit #2
Watchful Perch #3
Dark Patrol #4
Nevermore Run #5
```

Wordlists embedded in the daemon binary. ~200 adjectives × ~50 traits = 10,000+ unique names before collision.

## Web UI

The web UI favicon and header use a minimal raven silhouette SVG. Confidence tiers on the map use raven-themed marker colors:

| Confidence | Color | Description |
|---|---|---|
| HIGH (3) | `#1a1a2e` (near black) | Full raven |
| MEDIUM (2) | `#4a4a6e` (dark purple) | Silhouette |
| LOW (1) | `#8a8aae` (muted) | Outline |

## Project name

The new repository may use a raven-themed name. Options:

- `ravenwatch` — watching the Ravens
- `corvid` — raven family, also "covert ID"
- `poe` — mascot name, memorable, short
- Keep `flockdar` — established, descriptive

This is an open decision. The mascot is confirmed regardless of repository name.

## Consequences

- T-Deck firmware: replace emoji mood states with ASCII raven art
- Daemon: run name generator uses raven-themed wordlists
- Web UI: raven SVG favicon, confidence marker colors
- Boot screen: "Poe is watching..." instead of generic splash
- All user-facing strings use Poe's name for personality
