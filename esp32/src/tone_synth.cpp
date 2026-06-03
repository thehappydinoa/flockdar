// tone_synth.cpp — see tone_synth.h. Pure sample math, no hardware.
#include "tone_synth.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const uint16_t kEnvMs = 5;  // attack/release ramp per note (anti-click)

size_t cue_sample_count(const ToneCue *cue) {
  size_t total = 0;
  for (uint8_t i = 0; i < cue->count && i < TONE_MAX_NOTES; i++) {
    total += (size_t)cue->notes[i].dur_ms * TONE_SAMPLE_RATE / 1000;
  }
  return total;
}

size_t cue_render(const ToneCue *cue, int16_t *out, size_t cap) {
  size_t w = 0;
  const double amp = (double)cue->volume / 255.0 * 32767.0;
  const size_t env = (size_t)kEnvMs * TONE_SAMPLE_RATE / 1000;

  for (uint8_t i = 0; i < cue->count && i < TONE_MAX_NOTES; i++) {
    const ToneNote *n = &cue->notes[i];
    const size_t ns = (size_t)n->dur_ms * TONE_SAMPLE_RATE / 1000;
    for (size_t s = 0; s < ns; s++) {
      if (w >= cap) return w;
      int16_t sample = 0;
      if (n->freq_hz > 0) {
        // Square wave (sign of sine): louder than a pure sine through a
        // class-D amp, and trivial to compute.
        const double phase =
            2.0 * M_PI * (double)n->freq_hz * (double)s / TONE_SAMPLE_RATE;
        double v = (sin(phase) >= 0.0) ? 1.0 : -1.0;
        // Raised-cosine attack/release envelope to kill edge clicks.
        double g = 1.0;
        if (env > 0) {
          if (s < env) {
            g = 0.5 * (1.0 - cos(M_PI * (double)s / (double)env));
          } else if (s >= ns - env && ns > env) {
            g = 0.5 * (1.0 - cos(M_PI * (double)(ns - s) / (double)env));
          }
        }
        sample = (int16_t)(v * amp * g);
      }
      out[w++] = sample;
    }
  }
  return w;
}

ToneCue cue_for_confidence(uint8_t confidence) {
  ToneCue c = {};
  c.volume = 200;
  if (confidence >= 3) {
    // HIGH: urgent rising triple-beep.
    c.notes[0] = (ToneNote){880, 90};
    c.notes[1] = (ToneNote){0, 40};
    c.notes[2] = (ToneNote){1175, 90};
    c.notes[3] = (ToneNote){0, 40};
    c.notes[4] = (ToneNote){1568, 140};
    c.count = 5;
  } else if (confidence == 2) {
    // MEDIUM: two-note ping.
    c.notes[0] = (ToneNote){988, 90};
    c.notes[1] = (ToneNote){0, 40};
    c.notes[2] = (ToneNote){1319, 110};
    c.count = 3;
    c.volume = 170;
  } else {
    // LOW: single soft blip.
    c.notes[0] = (ToneNote){784, 70};
    c.count = 1;
    c.volume = 130;
  }
  return c;
}

ToneCue cue_boot(void) {
  ToneCue c = {};
  c.volume = 160;
  c.notes[0] = (ToneNote){660, 80};
  c.notes[1] = (ToneNote){880, 80};
  c.notes[2] = (ToneNote){1320, 120};
  c.count = 3;
  return c;
}
