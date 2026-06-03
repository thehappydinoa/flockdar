// tone_synth.h — pure 16-bit PCM tone synthesis for the T-Deck speaker.
//
// No hardware dependencies (no I2S, no Arduino) so the sample math is
// host-unit-testable. The audio driver (audio.cpp) feeds these samples to the
// MAX98357A I2S amp. A "cue" is a short sequence of (frequency, duration) notes
// with a fixed sample rate; cue_render() fills a caller-supplied mono buffer
// and a soft attack/release envelope avoids click/pop at note edges.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TONE_SAMPLE_RATE 16000  // Hz; plenty for alert chirps, low RAM
#define TONE_MAX_NOTES 8

typedef struct {
  uint16_t freq_hz;   // 0 = silence (rest)
  uint16_t dur_ms;
} ToneNote;

typedef struct {
  ToneNote notes[TONE_MAX_NOTES];
  uint8_t count;
  uint8_t volume;     // 0..255 peak amplitude scale
} ToneCue;

// Total samples a cue renders to at TONE_SAMPLE_RATE.
size_t cue_sample_count(const ToneCue *cue);

// Render the whole cue into `out` (mono int16). Writes at most `cap` samples;
// returns the number actually written. A 5 ms raised-cosine attack/release per
// note prevents clicks. Square-ish wave (sign of sine) for a clear, loud chirp.
size_t cue_render(const ToneCue *cue, int16_t *out, size_t cap);

// Built-in cues for detection confidence tiers (HIGH=3, MED=2, LOW=1) plus a
// boot chirp. Returned by value so callers can tweak volume before playing.
ToneCue cue_for_confidence(uint8_t confidence);
ToneCue cue_boot(void);

#ifdef __cplusplus
}
#endif
