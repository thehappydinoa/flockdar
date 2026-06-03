"""Host cross-check of esp32/src/tone_synth.cpp (the pure speaker tone core).

Compiles the synth with the tone_dump.c harness and verifies sample counts,
envelope/peak behaviour, buffer-cap safety, and that rests are silent. Skipped
if no C compiler is available. Mirrors tests/test_meshtastic_proto.py style.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

ESP32 = Path(__file__).resolve().parents[1] / "esp32"
SRC = ESP32 / "src" / "tone_synth.cpp"
HARNESS = ESP32 / "tests" / "tone_dump.cpp"

_CXX = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
pytestmark = pytest.mark.skipif(_CXX is None, reason="no C++ compiler for firmware host test")

SAMPLE_RATE = 16000


@pytest.fixture(scope="module")
def dump(tmp_path_factory) -> dict[str, dict[str, int]]:
    out = tmp_path_factory.mktemp("tone") / "tone_dump"
    subprocess.run(
        [_CXX, "-std=gnu++17", "-Wall", "-Werror", "-lm",
         str(HARNESS), str(SRC), "-o", str(out)],
        check=True,
    )
    res = subprocess.run([str(out)], capture_output=True, text=True, check=True)
    rows: dict[str, dict[str, int]] = {}
    for line in res.stdout.splitlines():
        parts = line.split()
        label = parts[0]
        rows[label] = {k: int(v) for k, v in (p.split("=") for p in parts[1:])}
    return rows


def test_sample_count_matches_predicted(dump) -> None:
    for label in ("HIGH", "MED", "LOW", "BOOT"):
        assert dump[label]["count"] == dump[label]["expected"], label


def test_high_cue_duration(dump) -> None:
    # HIGH = 90+40+90+40+140 ms = 400 ms -> 6400 samples @ 16 kHz.
    assert dump["HIGH"]["count"] == 400 * SAMPLE_RATE // 1000


def test_low_cue_is_short_single_blip(dump) -> None:
    # LOW = 70 ms -> 1120 samples.
    assert dump["LOW"]["count"] == 70 * SAMPLE_RATE // 1000


def test_peak_amplitude_scales_with_volume(dump) -> None:
    # HIGH volume 200 should be louder than LOW volume 130.
    assert dump["HIGH"]["peak"] > dump["LOW"]["peak"]
    # And within the int16 range.
    assert 0 < dump["HIGH"]["peak"] <= 32767


def test_buffer_cap_respected(dump) -> None:
    # Rendering into a 10-sample buffer must stop at 10, never overflow.
    assert dump["CAP"]["got"] == 10


def test_rest_is_silent(dump) -> None:
    assert dump["REST"]["peak"] == 0
    assert dump["REST"]["count"] == 20 * SAMPLE_RATE // 1000
