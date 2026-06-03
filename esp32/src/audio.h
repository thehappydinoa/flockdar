// audio.h — optional speaker alert cues (enable with -DFD_ENABLE_AUDIO).
//
// Drives the T-Deck's MAX98357A I2S amplifier. Detection alerts play a short
// confidence-tiered chirp without blocking the scan loop (a background task
// owns the I2S write). Tone math lives in tone_synth.* (pure, host-tested).
#pragma once

#include <stdint.h>

#include "tone_synth.h"

#if defined(FD_ENABLE_AUDIO)

// Init I2S + the playback task. Safe to call once from setup().
void audio_begin();

// Queue a cue for playback (non-blocking). Drops the request if one is already
// playing or audio is muted, so rapid detections never back up.
void audio_play(const ToneCue &cue);

// Convenience: confidence-tiered detection alert (1=LOW..3=HIGH).
void audio_alert(uint8_t confidence);

// Runtime mute toggle (persisted by the caller if desired).
void audio_set_muted(bool muted);
bool audio_muted();

#else  // audio disabled — no-op stubs keep call sites clean.

static inline void audio_begin() {}
static inline void audio_alert(uint8_t) {}
static inline void audio_set_muted(bool) {}
static inline bool audio_muted() { return true; }

#endif
