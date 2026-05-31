#include "sdlog.h"

#ifdef FD_ENABLE_SD

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#if defined(FD_ENABLE_TDECK_UI)
#include "tdeck_board.h"
#endif

static File s_file;
static bool s_ok = false;
static char s_path[24] = "";
static uint32_t s_last_flush = 0;
static bool s_dirty = false;
static uint32_t s_last_mount_try = 0;

#if defined(FD_ENABLE_TDECK_UI)
static bool tdeck_sd_mount() {
  tdeck_spi_idle();
  delay(10);
  for (int attempt = 0; attempt < 3; attempt++) {
    if (SD.begin(TDECK_SD_CS, SPI, 800000U)) {
      if (SD.cardType() != CARD_NONE) {
        return true;
      }
      SD.end();
    }
    delay(100);
  }
  return false;
}
#endif

static bool open_log_file() {
  for (int i = 1; i < 10000; i++) {
    snprintf(s_path, sizeof(s_path), "/flock-%04d.ndjson", i);
    if (!SD.exists(s_path)) {
      break;
    }
  }
  s_file = SD.open(s_path, FILE_WRITE);
  if (!s_file) {
    return false;
  }
  s_last_flush = millis();
  return true;
}

void sdlog_begin() {
#if defined(FD_ENABLE_TDECK_UI)
  if (!tdeck_sd_mount()) {
    s_ok = false;
    s_last_mount_try = millis();
    return;
  }
#else
  SPI.begin(FD_SD_SCK, FD_SD_MISO, FD_SD_MOSI, FD_SD_CS);
  if (!SD.begin(FD_SD_CS)) {
    s_ok = false;
    return;
  }
#endif
  s_ok = open_log_file();
  s_last_mount_try = millis();
}

bool sdlog_ok() { return s_ok; }

const char *sdlog_path() { return s_ok ? s_path : ""; }

void sdlog_write(const char *line) {
  if (!s_ok) return;
#if defined(FD_ENABLE_TDECK_UI)
  tdeck_spi_idle();
#endif
  s_file.print(line);
  s_file.print('\n');
  s_dirty = true;
}

void sdlog_loop() {
#if defined(FD_ENABLE_TDECK_UI)
  if (!s_ok && millis() - s_last_mount_try >= 5000) {
    s_last_mount_try = millis();
    if (tdeck_sd_mount() && open_log_file()) {
      s_ok = true;
    }
  }
#endif
  if (!s_ok || !s_dirty) return;
  uint32_t now = millis();
  if (now - s_last_flush < FD_SD_FLUSH_MS) return;
#if defined(FD_ENABLE_TDECK_UI)
  tdeck_spi_idle();
#endif
  s_file.flush();
  s_last_flush = now;
  s_dirty = false;
}

#endif  // FD_ENABLE_SD
