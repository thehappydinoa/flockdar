#include "sdlog.h"

#ifdef FD_ENABLE_SD

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

static File s_file;
static bool s_ok = false;
static char s_path[24] = "";
static uint32_t s_last_flush = 0;
static bool s_dirty = false;

void sdlog_begin() {
  // Validated, board-specific SPI pins from pins.h (via config.h).
  SPI.begin(FD_SD_SCK, FD_SD_MISO, FD_SD_MOSI, FD_SD_CS);
  if (!SD.begin(FD_SD_CS)) {
    s_ok = false;
    return;
  }
  // Pick the next unused /flock-NNNN.ndjson so each boot gets a fresh file.
  for (int i = 1; i < 10000; i++) {
    snprintf(s_path, sizeof(s_path), "/flock-%04d.ndjson", i);
    if (!SD.exists(s_path)) break;
  }
  s_file = SD.open(s_path, FILE_WRITE);
  s_ok = (bool)s_file;
  s_last_flush = millis();
}

bool sdlog_ok() { return s_ok; }

const char *sdlog_path() { return s_ok ? s_path : ""; }

void sdlog_write(const char *line) {
  if (!s_ok) return;
  s_file.print(line);
  s_file.print('\n');
  s_dirty = true;
}

void sdlog_loop() {
  if (!s_ok || !s_dirty) return;
  uint32_t now = millis();
  if (now - s_last_flush < FD_SD_FLUSH_MS) return;
  s_file.flush();
  s_last_flush = now;
  s_dirty = false;
}

#endif  // FD_ENABLE_SD
