#include "sdlog.h"

#ifdef FD_ENABLE_SD

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

#if defined(FD_ENABLE_TDECK_UI)
#include "tdeck_board.h"
#include "tdeck_ui.h"
#endif

#include "serial_out.h"

static File s_file;
static bool s_ok = false;
static char s_path[24] = "";
static uint32_t s_last_flush = 0;
static bool s_dirty = false;
static uint32_t s_last_mount_try = 0;

static char s_last_err[24] = "not tried";
static uint32_t s_mount_attempts = 0;
static uint32_t s_last_speed_hz = 0;
static uint8_t s_card_type = CARD_NONE;
static int s_miso_level = -1;

static void set_err(const char *msg) {
  strncpy(s_last_err, msg, sizeof(s_last_err) - 1);
  s_last_err[sizeof(s_last_err) - 1] = '\0';
}

static const char *card_type_name(uint8_t t) {
  switch (t) {
  case CARD_NONE:
    return "none";
  case CARD_MMC:
    return "mmc";
  case CARD_SD:
    return "sd";
  case CARD_SDHC:
    return "sdhc";
  default:
    return "?";
  }
}

#if defined(FD_ENABLE_TDECK_UI)
static bool tdeck_sd_mount(bool hard_reset) {
  s_mount_attempts++;
  return tdeck_mount_sd(hard_reset, s_last_err, sizeof(s_last_err),
                        &s_last_speed_hz, &s_card_type, &s_miso_level);
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
    set_err("open fail");
    return false;
  }
  s_last_flush = millis();
  return true;
}

static void sdlog_release_bus() {
  if (s_file) {
    s_file.close();
  }
#if defined(FD_ENABLE_TDECK_UI)
  SD.end();
#endif
}

static bool mount_and_open(bool hard_reset) {
#if defined(FD_ENABLE_TDECK_UI)
  sdlog_release_bus();
  if (!tdeck_sd_mount(hard_reset)) {
    s_ok = false;
    return false;
  }
#else
  sdlog_release_bus();
  SPI.begin(FD_SD_SCK, FD_SD_MISO, FD_SD_MOSI, FD_SD_CS);
  if (!SD.begin(FD_SD_CS)) {
    set_err("begin fail");
    s_ok = false;
    return false;
  }
  s_card_type = SD.cardType();
#endif
  if (!open_log_file()) {
    sdlog_release_bus();
    s_ok = false;
    return false;
  }
  s_ok = true;
  return true;
}

void sdlog_begin() {
#if defined(FD_ENABLE_TDECK_UI)
  pinMode(TDECK_SD_CS, OUTPUT);
  digitalWrite(TDECK_SD_CS, HIGH);
#endif
  if (!mount_and_open(false)) {
    s_last_mount_try = millis();
    char msg[64];
    snprintf(msg, sizeof(msg), "sd mount fail: %s type=%s miso=%d",
             s_last_err, card_type_name(s_card_type), s_miso_level);
    serial_out_info(msg);
    return;
  }
  s_last_mount_try = millis();
  char msg[64];
  snprintf(msg, sizeof(msg), "sd log open %s (%s)", s_path,
           card_type_name(s_card_type));
  serial_out_info(msg);
}

bool sdlog_ok() { return s_ok; }

const char *sdlog_path() { return s_ok ? s_path : ""; }

void sdlog_get_status(SdlogStatus *out) {
  if (!out) return;
  out->mounted = (s_card_type != CARD_NONE);
  out->logging = s_ok;
  out->card_type = s_card_type;
  out->mount_attempts = s_mount_attempts;
  out->last_speed_hz = s_last_speed_hz;
  out->miso_level = s_miso_level;
  strncpy(out->card_type_name, card_type_name(s_card_type),
          sizeof(out->card_type_name) - 1);
  strncpy(out->last_err, s_last_err, sizeof(out->last_err) - 1);
  if (s_ok) {
    strncpy(out->path, s_path, sizeof(out->path) - 1);
  } else {
    out->path[0] = '\0';
  }
}

bool sdlog_retry_mount() {
  if (s_ok) return true;
  sdlog_release_bus();
  s_last_mount_try = 0;
  if (mount_and_open(true)) {
    serial_out_info("sd remount ok");
    return true;
  }
  char msg[64];
  snprintf(msg, sizeof(msg), "sd remount fail: %s", s_last_err);
  serial_out_info(msg);
  s_last_mount_try = millis();
  return false;
}

void sdlog_write(const char *line) {
  if (!s_ok) return;
#if defined(FD_ENABLE_TDECK_UI)
  tdeck_spi_release();
#endif
  s_file.print(line);
  s_file.print('\n');
  s_dirty = true;
}

void sdlog_loop() {
#if defined(FD_ENABLE_TDECK_UI)
  if (!s_ok && millis() - s_last_mount_try >= 5000) {
    s_last_mount_try = millis();
    mount_and_open(false);
  }
#endif
  if (!s_ok || !s_dirty) return;
  uint32_t now = millis();
  if (now - s_last_flush < FD_SD_FLUSH_MS) return;
#if defined(FD_ENABLE_TDECK_UI)
  tdeck_spi_release();
#endif
  s_file.flush();
  s_last_flush = now;
  s_dirty = false;
}

#endif  // FD_ENABLE_SD
