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

static bool s_host_busy = false;
static bool s_dumping = false;
static bool s_dump_resume_log = false;
static File s_dump_file;
static char s_dump_path[32] = "";
static uint32_t s_dump_lines = 0;
static uint32_t s_dump_bytes = 0;

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
  if (!s_ok || s_host_busy) return;
#if defined(FD_ENABLE_TDECK_UI)
  tdeck_spi_release();
#endif
  s_file.print(line);
  s_file.print('\n');
  s_dirty = true;
}

void sdlog_bus_hold(bool hold) { s_host_busy = hold; }

void sdlog_loop() {
  if (s_host_busy) {
    return;
  }
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

static bool flock_path_valid(const char *path) {
  if (!path || strncmp(path, "/flock-", 7) != 0) {
    return false;
  }
  const char *dot = strrchr(path, '.');
  return dot && strcmp(dot, ".ndjson") == 0;
}

static int flock_path_num(const char *path) {
  int n = -1;
  if (path) {
    sscanf(path, "/flock-%d.ndjson", &n);
  }
  return n;
}

static void sd_bus_release() {
#if defined(FD_ENABLE_TDECK_UI)
  tdeck_spi_release();
#endif
}

void sdlog_list() {
  if (s_dumping) {
    serial_out_info("sd list fail: busy");
    return;
  }
  if (!s_ok && !mount_and_open(false)) {
    serial_out_info("sd list fail: not mounted");
    return;
  }

  sd_bus_release();
  File root = SD.open("/");
  if (!root) {
    serial_out_info("sd list fail: open /");
    return;
  }

  serial_out_info("sd list begin");
  s_host_busy = true;
  int count = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    const char *name = entry.name();
    char path[32];
    if (name[0] == '/') {
      strncpy(path, name, sizeof(path) - 1);
    } else {
      snprintf(path, sizeof(path), "/%s", name);
    }
    path[sizeof(path) - 1] = '\0';
    if (flock_path_valid(path)) {
      char msg[64];
      snprintf(msg, sizeof(msg), "sd file %s %lu", path,
               (unsigned long)entry.size());
      serial_out_info(msg);
      count++;
    }
    entry.close();
  }
  root.close();

  char done[32];
  snprintf(done, sizeof(done), "sd list end %d", count);
  serial_out_info(done);
  s_host_busy = false;
}

static void resolve_dump_path(const char *arg, char *out, size_t outsz) {
  out[0] = '\0';
  if (arg && arg[0] && strcmp(arg, "last") != 0) {
    if (arg[0] == '/') {
      strncpy(out, arg, outsz - 1);
    } else {
      snprintf(out, outsz, "/%s", arg);
    }
    out[outsz - 1] = '\0';
    return;
  }

  const int current = s_ok ? flock_path_num(s_path) : -1;
  int best_n = -1;
  for (int i = 1; i < 10000; i++) {
    char p[24];
    snprintf(p, sizeof(p), "/flock-%04d.ndjson", i);
    if (!SD.exists(p)) {
      continue;
    }
    if (strcmp(arg, "last") == 0 && s_ok && i == current) {
      continue;
    }
    if (i > best_n) {
      best_n = i;
    }
  }
  if (best_n >= 0) {
    snprintf(out, outsz, "/flock-%04d.ndjson", best_n);
    return;
  }
  if (s_ok) {
    strncpy(out, s_path, outsz - 1);
    out[outsz - 1] = '\0';
  }
}

bool sdlog_host_busy() { return s_host_busy; }

// --- chunked SD dump (yields to main loop; host can abort) -----------------

static void sdlog_resume_write_log() {
  if (!s_dump_resume_log) {
    return;
  }
  s_dump_resume_log = false;
  s_file = SD.open(s_path, FILE_APPEND);
  if (!s_file) {
    s_ok = false;
    set_err("reopen fail");
  } else {
    s_ok = true;
  }
}

void sdlog_dump_abort() {
  if (!s_dumping) {
    return;
  }
  s_dump_file.close();
  s_dumping = false;
  s_host_busy = false;
  sdlog_resume_write_log();
  serial_out_info("sd dump aborted");
}

static void sdlog_pause_write_log() {
  if (!s_ok) {
    return;
  }
  sd_bus_release();
  s_file.flush();
  s_file.close();
  s_dump_resume_log = true;
  s_ok = false;
}

bool sdlog_dump(const char *path) {
  if (s_dumping) {
    serial_out_info("sd dump fail: busy");
    return false;
  }
  if (!s_ok && !mount_and_open(false)) {
    serial_out_info("sd dump fail: not mounted");
    return false;
  }

  char target[32];
  resolve_dump_path(path, target, sizeof(target));
  if (!flock_path_valid(target)) {
    serial_out_info("sd dump fail: no file");
    return false;
  }
  if (!SD.exists(target)) {
    serial_out_info("sd dump fail: not found");
    return false;
  }

  sd_bus_release();
  sdlog_pause_write_log();

  s_dump_file = SD.open(target, FILE_READ);
  if (!s_dump_file) {
    serial_out_info("sd dump fail: open");
    sdlog_resume_write_log();
    return false;
  }

  s_dump_bytes = s_dump_file.size();
  strncpy(s_dump_path, target, sizeof(s_dump_path) - 1);
  s_dump_path[sizeof(s_dump_path) - 1] = '\0';
  s_dump_lines = 0;
  s_dumping = true;
  s_host_busy = true;

  char ack[72];
  snprintf(ack, sizeof(ack), "sd dump ack %s %lu", target,
           (unsigned long)s_dump_bytes);
  serial_out_info(ack);

  char hdr[72];
  snprintf(hdr, sizeof(hdr), "sd dump begin %s %lu", target,
           (unsigned long)s_dump_bytes);
  serial_out_info(hdr);
  return true;
}

bool sdlog_dump_poll() {
  if (!s_dumping) {
    return false;
  }

  static char batch[3072];
  size_t batch_n = 0;

  auto flush_batch = [&]() {
    if (batch_n > 0) {
      serial_out_usb_write((const uint8_t *)batch, batch_n);
      batch_n = 0;
    }
  };

  auto emit_line = [&](const char *line, size_t len) {
    if (len == 0) {
      return;
    }
    if (batch_n + len + 1 > sizeof(batch)) {
      flush_batch();
    }
    if (len + 1 > sizeof(batch)) {
      char buf[520];
      if (len + 1 <= sizeof(buf)) {
        memcpy(buf, line, len);
        buf[len] = '\n';
        serial_out_usb_write((const uint8_t *)buf, len + 1);
      } else {
        serial_out_usb_write((const uint8_t *)line, len);
        const char nl = '\n';
        serial_out_usb_write((const uint8_t *)&nl, 1);
      }
      return;
    }
    memcpy(batch + batch_n, line, len);
    batch_n += len;
    batch[batch_n++] = '\n';
  };

  static char line[512];
  const uint32_t until = millis() + 40;
  while (millis() < until && s_dump_file.available()) {
    size_t n = 0;
    while (s_dump_file.available()) {
      const int c = s_dump_file.read();
      if (c < 0) {
        break;
      }
      if (c == '\n' || c == '\r') {
        if (c == '\r' && s_dump_file.peek() == '\n') {
          s_dump_file.read();
        }
        break;
      }
      if (n + 1 < sizeof(line)) {
        line[n++] = (char)c;
      }
    }
    if (n == 0) {
      continue;
    }
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) {
      n--;
    }
    if (n == 0) {
      continue;
    }
    emit_line(line, n);
    s_dump_lines++;
  }
  flush_batch();

  if (s_dump_file.available()) {
    return true;
  }

  s_dump_file.close();
  s_dumping = false;
  s_host_busy = false;

  char done[56];
  snprintf(done, sizeof(done), "sd dump end %s %lu", s_dump_path,
           (unsigned long)s_dump_lines);
  serial_out_info(done);
  sdlog_resume_write_log();
  return false;
}

#endif  // FD_ENABLE_SD
