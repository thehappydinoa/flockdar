"""Pin-spec validation for the ESP32 firmware (esp32/pin_spec.py).

Keeps the hardware design honest: every board's GPIO map must pass the chip
constraint rules, and the generated src/pins.h must stay in sync with the spec.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

ESP32_DIR = Path(__file__).resolve().parents[1] / "esp32"
sys.path.insert(0, str(ESP32_DIR))

import pin_spec  # noqa: E402  (path injected above)


@pytest.mark.parametrize("board", pin_spec.BOARDS, ids=lambda b: b.key)
def test_board_has_no_errors(board: pin_spec.Board) -> None:
    errors = [i for i in pin_spec.validate(board) if i.severity == pin_spec.ERROR]
    assert not errors, "\n".join(f"{i.signal}/GPIO{i.pin}: {i.message}" for i in errors)


@pytest.mark.parametrize("board", pin_spec.BOARDS, ids=lambda b: b.key)
def test_board_has_no_pin_conflicts(board: pin_spec.Board) -> None:
    pins = list(board.pins.values())
    assert len(pins) == len(set(pins)), f"duplicate GPIO in {board.key}: {pins}"


@pytest.mark.parametrize("board", pin_spec.BOARDS, ids=lambda b: b.key)
def test_every_signal_assigned(board: pin_spec.Board) -> None:
    for sig in pin_spec.SIGNALS:
        assert sig.key in board.pins, f"{sig.macro} unassigned on {board.key}"


def test_validate_all_returns_success() -> None:
    assert pin_spec.validate_all() == 0


def test_pins_header_in_sync() -> None:
    """src/pins.h must match `pin_spec.py gen` output (no hand edits)."""
    on_disk = pin_spec.HEADER_PATH.read_text(encoding="utf-8")
    assert on_disk == pin_spec.render_header(), (
        "esp32/src/pins.h is stale — run `uv run esp32/pin_spec.py gen`"
    )


def test_validator_catches_nonexistent_pin() -> None:
    """Regression guard: the old default MOSI=23 is invalid on the ESP32-S3."""
    bad = pin_spec.Board(
        key="bad",
        board_id="x",
        chip="esp32s3",
        target_macro="X",
        gps_uart=1,
        pins={
            "OLED_SDA": 8,
            "OLED_SCL": 9,
            "SD_CS": 10,
            "SD_SCK": 12,
            "SD_MISO": 19,
            "SD_MOSI": 23,
            "GPS_RX": 18,
            "GPS_TX": 17,
        },
    )
    issues = pin_spec.validate(bad)
    errors = [i for i in issues if i.severity == pin_spec.ERROR]
    warns = [i for i in issues if i.severity == pin_spec.WARN]
    assert any("does not exist" in i.message for i in errors)  # GPIO23
    assert any("USB" in i.message for i in warns)  # GPIO19
