#!/usr/bin/env python3
"""
Authoritative GPIO pin assignments for flockdar-esp32, with a validator.

This is the single source of truth for `esp32/src/pins.h`. Pin choices are
checked against each target chip's GPIO rules (non-existent pins, SPI-flash /
PSRAM pins, USB pins, strapping pins, input-only pins, and intra-board
conflicts) so a bad assignment fails loudly instead of bricking a board or
silently breaking native USB.

    uv run esp32/pin_spec.py validate    # check every board's pin map
    uv run esp32/pin_spec.py gen         # regenerate esp32/src/pins.h

`validate` exits non-zero if any board has an ERROR (warnings do not fail).

References:
  - ESP32-S3 datasheet §2.1 (pin definitions) / TRM ch. "IO MUX & GPIO"
  - ESP32 datasheet §2.2 + "ESP32 strapping pins" / ADC2+Wi-Fi note
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path

HEADER_PATH = Path(__file__).parent / "src" / "pins.h"

# Signal direction at the ESP32 side (drives input-only validation).
OUT, IN, INOUT = "out", "in", "inout"


# ---------------------------------------------------------------------------
# Signals — the GPIO-bearing pins the firmware drives
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Signal:
    key: str        # internal name, e.g. "SD_MISO"
    macro: str      # C macro emitted into pins.h
    direction: str  # OUT / IN / INOUT (from the ESP32's perspective)
    desc: str


SIGNALS: list[Signal] = [
    Signal("OLED_SDA", "FD_OLED_SDA", INOUT, "I2C data  (SSD1306 OLED)"),
    Signal("OLED_SCL", "FD_OLED_SCL", INOUT, "I2C clock (SSD1306 OLED)"),
    Signal("SD_CS",    "FD_SD_CS",    OUT,   "SPI chip-select (microSD)"),
    Signal("SD_SCK",   "FD_SD_SCK",   OUT,   "SPI clock       (microSD)"),
    Signal("SD_MISO",  "FD_SD_MISO",  IN,    "SPI MISO  (microSD -> ESP)"),
    Signal("SD_MOSI",  "FD_SD_MOSI",  OUT,   "SPI MOSI  (ESP -> microSD)"),
    Signal("GPS_RX",   "FD_GPS_RX_PIN", IN,  "UART RX   (GPS TX -> ESP)"),
    Signal("GPS_TX",   "FD_GPS_TX_PIN", OUT, "UART TX   (ESP -> GPS RX)"),
]
SIGNAL_BY_KEY = {s.key: s for s in SIGNALS}


# ---------------------------------------------------------------------------
# Chip GPIO capability database
# ---------------------------------------------------------------------------

@dataclass
class ChipCaps:
    name: str
    valid: set[int]                 # GPIO numbers that physically exist
    flash: set[int] = field(default_factory=set)        # dedicated SPI-flash
    psram_octal: set[int] = field(default_factory=set)  # used on octal-PSRAM modules
    usb: set[int] = field(default_factory=set)          # native USB D-/D+
    strapping: set[int] = field(default_factory=set)    # boot strapping
    input_only: set[int] = field(default_factory=set)   # no output driver
    console: set[int] = field(default_factory=set)      # default UART0 console
    notes: dict[int, str] = field(default_factory=dict)  # informational only


# ESP32-S3: GPIO0-21 and GPIO26-48 exist (22/23/24/25 do NOT).
ESP32S3 = ChipCaps(
    name="ESP32-S3",
    valid=set(range(0, 22)) | set(range(26, 49)),
    flash={26, 27, 28, 29, 30, 31, 32},          # SPI0/1 flash
    psram_octal={33, 34, 35, 36, 37},            # octal PSRAM/flash (…R8 modules)
    usb={19, 20},                                # USB-Serial/JTAG D-/D+
    strapping={0, 3, 45, 46},
    input_only=set(),                            # S3 has none
    console={43, 44},                            # UART0 TXD0/RXD0
    notes={48: "onboard RGB LED on DevKitC-1", 38: "onboard RGB LED on some S3 boards"},
)

# ESP32 (WROOM-32): typical DevKit breakout. GPIO6-11 are SPI flash.
ESP32 = ChipCaps(
    name="ESP32",
    valid={0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25,
           26, 27, 32, 33, 34, 35, 36, 39},
    flash={6, 7, 8, 9, 10, 11},
    usb=set(),                                   # no native USB
    strapping={0, 2, 5, 12, 15},
    input_only={34, 35, 36, 39},
    console={1, 3},                              # UART0 TX/RX
    notes={16: "in use on WROVER (PSRAM) modules", 17: "in use on WROVER (PSRAM) modules"},
)

CHIPS = {"esp32s3": ESP32S3, "esp32": ESP32}


# ---------------------------------------------------------------------------
# Boards — a validated pin map per target
# ---------------------------------------------------------------------------

@dataclass
class Board:
    key: str            # human label
    board_id: str       # PlatformIO board / DevKit
    chip: str           # key into CHIPS
    target_macro: str   # Arduino-core target define used in pins.h
    gps_uart: int       # UART peripheral instance for the GPS
    pins: dict[str, int]  # Signal.key -> GPIO


BOARDS: list[Board] = [
    Board(
        key="ESP32-S3",
        board_id="esp32-s3-devkitc-1",
        chip="esp32s3",
        target_macro="CONFIG_IDF_TARGET_ESP32S3",
        gps_uart=1,
        # Conflict-free general-purpose pins: avoids USB (19/20), flash
        # (26-32), octal PSRAM (33-37), strapping (0/3/45/46) and the UART0
        # console (43/44).
        pins={
            "OLED_SDA": 8,  "OLED_SCL": 9,
            "SD_CS": 10, "SD_SCK": 12, "SD_MISO": 13, "SD_MOSI": 11,
            "GPS_RX": 18, "GPS_TX": 17,
        },
    ),
    Board(
        key="ESP32",
        board_id="esp32dev",
        chip="esp32",
        target_macro="CONFIG_IDF_TARGET_ESP32",
        gps_uart=2,
        # Classic VSPI + UART2 + default I2C. GPIO5 (CS) is a strapping pin —
        # acceptable (has a boot-time pull-up) and warned about below.
        pins={
            "OLED_SDA": 21, "OLED_SCL": 22,
            "SD_CS": 5, "SD_SCK": 18, "SD_MISO": 19, "SD_MOSI": 23,
            "GPS_RX": 16, "GPS_TX": 17,
        },
    ),
]


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------

ERROR, WARN = "ERROR", "WARN"


@dataclass
class Issue:
    severity: str
    signal: str
    pin: int
    message: str


def _pin_flags(chip: ChipCaps, pin: int, direction: str) -> list[Issue]:
    """All issues for a single pin used in a given direction."""
    issues: list[Issue] = []
    if pin not in chip.valid:
        issues.append(Issue(ERROR, "", pin, f"GPIO{pin} does not exist on {chip.name}"))
        return issues  # nothing else meaningful
    if pin in chip.flash:
        issues.append(Issue(ERROR, "", pin, f"GPIO{pin} is a dedicated SPI-flash pin"))
    if pin in chip.input_only and direction in (OUT, INOUT):
        issues.append(Issue(ERROR, "", pin, f"GPIO{pin} is input-only; cannot drive a '{direction}' signal"))
    if pin in chip.usb:
        issues.append(Issue(WARN, "", pin, f"GPIO{pin} is a native USB pin — breaks USB-Serial/JTAG if used"))
    if pin in chip.psram_octal:
        issues.append(Issue(WARN, "", pin, f"GPIO{pin} is used by octal PSRAM/flash (…R8 modules)"))
    if pin in chip.strapping:
        issues.append(Issue(WARN, "", pin, f"GPIO{pin} is a strapping pin — keep its boot level free"))
    if pin in chip.console:
        issues.append(Issue(WARN, "", pin, f"GPIO{pin} is the default UART0 console"))
    return issues


def validate(board: Board) -> list[Issue]:
    """Return every issue (ERROR/WARN) for a board's pin map."""
    chip = CHIPS[board.chip]
    issues: list[Issue] = []

    # Per-pin rule checks.
    for sig in SIGNALS:
        pin = board.pins.get(sig.key)
        if pin is None:
            issues.append(Issue(ERROR, sig.key, -1, f"{sig.macro} is unassigned"))
            continue
        for iss in _pin_flags(chip, pin, sig.direction):
            issues.append(Issue(iss.severity, sig.key, iss.pin, iss.message))

    # Intra-board conflicts (same GPIO on two functions).
    used: dict[int, list[str]] = {}
    for sig in SIGNALS:
        pin = board.pins.get(sig.key)
        if pin is not None:
            used.setdefault(pin, []).append(sig.key)
    for pin, sigs in used.items():
        if len(sigs) > 1:
            issues.append(Issue(ERROR, "+".join(sigs), pin,
                                f"GPIO{pin} assigned to multiple functions: {', '.join(sigs)}"))
    return issues


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def _note_for(chip: ChipCaps, pin: int) -> str:
    return chip.notes.get(pin, "")


def report(board: Board) -> tuple[str, int, int]:
    """Render a professional validation table for one board.

    Returns (text, n_errors, n_warnings).
    """
    chip = CHIPS[board.chip]
    issues = validate(board)
    by_sig: dict[str, list[Issue]] = {}
    for iss in issues:
        by_sig.setdefault(iss.signal, []).append(iss)

    lines = [
        f"Board: {board.key}  ({board.board_id}, chip={chip.name}, GPS=UART{board.gps_uart})",
        f"  {'FUNCTION':<10} {'GPIO':>4}  {'DIR':<5} {'STATUS':<6} NOTES",
        f"  {'-'*10} {'-'*4}  {'-'*5} {'-'*6} {'-'*40}",
    ]
    for sig in SIGNALS:
        pin = board.pins.get(sig.key)
        sig_issues = by_sig.get(sig.key, [])
        if any(i.severity == ERROR for i in sig_issues):
            status = "FAIL"
        elif sig_issues:
            status = "warn"
        else:
            status = "ok"
        msgs = [i.message for i in sig_issues]
        note = _note_for(chip, pin) if pin is not None else ""
        if note:
            msgs.append(note)
        pin_s = str(pin) if pin is not None else "—"
        lines.append(f"  {sig.key:<10} {pin_s:>4}  {sig.direction:<5} {status:<6} {'; '.join(msgs)}")

    # Conflict issues have a composite signal name not in SIGNALS.
    for iss in issues:
        if iss.signal not in SIGNAL_BY_KEY and iss.signal:
            lines.append(f"  {'CONFLICT':<10} {iss.pin:>4}  {'-':<5} {'FAIL':<6} {iss.message}")

    n_err = sum(1 for i in issues if i.severity == ERROR)
    n_warn = sum(1 for i in issues if i.severity == WARN)
    verdict = "PASS" if n_err == 0 else "FAIL"
    lines.append(f"  Result: {verdict}  ({n_err} error(s), {n_warn} warning(s))")
    return "\n".join(lines), n_err, n_warn


def validate_all() -> int:
    """Validate every board; print the report. Returns process exit code."""
    print("flockdar-esp32 pin validation")
    print("=" * 60)
    total_err = 0
    for board in BOARDS:
        text, n_err, _ = report(board)
        print(text)
        print()
        total_err += n_err
    if total_err:
        print(f"FAILED: {total_err} error(s) across {len(BOARDS)} board(s).")
        return 1
    print(f"OK: all {len(BOARDS)} board(s) validated.")
    return 0


# ---------------------------------------------------------------------------
# Header generation
# ---------------------------------------------------------------------------

def render_header() -> str:
    """Render esp32/src/pins.h from the validated board maps."""
    out = [
        "// pins.h — AUTO-GENERATED by esp32/pin_spec.py. DO NOT EDIT.",
        "// Regenerate:  uv run esp32/pin_spec.py gen   (run `validate` first)",
        "//",
        "// Board-conditional, validated GPIO assignments. See esp32/HARDWARE.md",
        "// for the full pin specification, rationale, and wiring.",
        "#pragma once",
        "",
    ]
    for i, board in enumerate(BOARDS):
        guard = "#if" if i == 0 else "#elif"
        out.append(f"{guard} defined({board.target_macro})  // {board.key} ({board.board_id})")
        for sig in SIGNALS:
            out.append(f"#define {sig.macro} {board.pins[sig.key]}  // {sig.desc}")
        out.append(f"#define FD_GPS_UART {board.gps_uart}")
        out.append("")
    out.append("#else")
    out.append('#error "flockdar-esp32: no validated pin map for this target — '
               'add one in esp32/pin_spec.py and run `pin_spec.py gen`."')
    out.append("#endif")
    out.append("")
    return "\n".join(out)


def write_header() -> None:
    HEADER_PATH.write_text(render_header(), encoding="utf-8")
    print(f"Wrote {HEADER_PATH}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="flockdar-esp32 pin spec validator / generator")
    ap.add_argument("command", choices=["validate", "gen"], nargs="?", default="validate")
    args = ap.parse_args(argv)

    if args.command == "validate":
        return validate_all()

    # gen: validate first, refuse to emit an invalid header.
    rc = validate_all()
    if rc != 0:
        print("Refusing to generate pins.h while validation fails.", file=sys.stderr)
        return rc
    write_header()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
