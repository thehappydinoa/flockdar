// tone_dump.c — host harness: exercises the pure tone_synth core and prints
// metrics the Python test (tests/test_tone_synth.py) cross-checks. No hardware.
#include <stdio.h>

#include "../src/tone_synth.h"

// Print: label sample_count first_sample peak nonzero_for_silence_rest
static void analyze(const char *label, const ToneCue *cue) {
  static int16_t buf[TONE_SAMPLE_RATE];  // up to 1 s
  size_t n = cue_render(cue, buf, TONE_SAMPLE_RATE);
  int peak = 0;
  for (size_t i = 0; i < n; i++) {
    int v = buf[i] < 0 ? -buf[i] : buf[i];
    if (v > peak) peak = v;
  }
  printf("%s count=%zu expected=%zu peak=%d\n", label, n, cue_sample_count(cue),
         peak);
}

int main(void) {
  ToneCue hi = cue_for_confidence(3);
  ToneCue med = cue_for_confidence(2);
  ToneCue lo = cue_for_confidence(1);
  ToneCue boot = cue_boot();
  analyze("HIGH", &hi);
  analyze("MED", &med);
  analyze("LOW", &lo);
  analyze("BOOT", &boot);

  // Cap enforcement: tiny buffer must not overflow.
  int16_t small[10];
  size_t got = cue_render(&hi, small, 10);
  printf("CAP got=%zu\n", got);

  // A pure rest renders silence (peak 0).
  ToneCue rest = {};
  rest.notes[0].freq_hz = 0;
  rest.notes[0].dur_ms = 20;
  rest.count = 1;
  rest.volume = 200;
  analyze("REST", &rest);
  return 0;
}
