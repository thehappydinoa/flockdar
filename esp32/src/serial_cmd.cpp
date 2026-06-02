#include "serial_cmd.h"

#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "serial_out.h"

#ifdef FD_ENABLE_GPS
#include "gps.h"
#endif
#ifdef FD_ENABLE_SD
#include "sdlog.h"
#endif

namespace {

constexpr size_t kCmdMax = 96;
char s_buf[kCmdMax];
size_t s_len = 0;

void handle_cmd(char *cmd) {
  while (*cmd == ' ' || *cmd == '\t') {
    cmd++;
  }
  if (*cmd == '\0') {
    return;
  }

#ifdef FD_ENABLE_GPS
  if (strncmp(cmd, "tz", 2) == 0 &&
      (cmd[2] == '\0' || cmd[2] == ' ' || cmd[2] == '\t')) {
    const char *arg = cmd + 2;
    while (*arg == ' ' || *arg == '\t') {
      arg++;
    }
    if (*arg == '\0') {
      char msg[48];
      snprintf(msg, sizeof(msg), "tz offset %d min (UTC%+d:%02u)",
               (int)gps_tz_offset_min(),
               (int)(gps_tz_offset_min() / 60),
               (unsigned)(abs(gps_tz_offset_min()) % 60));
      serial_out_info(msg);
      return;
    }
    char *end = nullptr;
    long value = strtol(arg, &end, 10);
    if (end == arg) {
      serial_out_info("tz usage: tz [-]minutes  (US Eastern: tz -300)");
      return;
    }
    while (*end == ' ' || *end == '\t') {
      end++;
    }
    if (*end == 'h' || *end == 'H') {
      value *= 60L;
    }
    if (value < -840L || value > 840L) {
      serial_out_info("tz out of range (-840..840 min)");
      return;
    }
    gps_tz_set_offset_min((int16_t)value);
    char msg[48];
    snprintf(msg, sizeof(msg), "tz set to %ld min (UTC%+ld:%02lu)", value,
             value / 60L, (unsigned long)(labs(value) % 60L));
    serial_out_info(msg);
    return;
  }
#endif

#ifdef FD_ENABLE_SD
  if (strncmp(cmd, "sd ", 3) == 0) {
    const char *sub = cmd + 3;
    while (*sub == ' ') {
      sub++;
    }
    if (strncmp(sub, "list", 4) == 0 &&
        (sub[4] == '\0' || sub[4] == ' ')) {
      sdlog_list();
      return;
    }
    if (strncmp(sub, "dump", 4) == 0) {
      const char *path = sub + 4;
      while (*path == ' ') {
        path++;
      }
      sdlog_dump(*path ? path : nullptr);
      return;
    }
    if (strncmp(sub, "abort", 5) == 0 &&
        (sub[5] == '\0' || sub[5] == ' ')) {
      sdlog_dump_abort();
      return;
    }
  }
#endif
}

}  // namespace

void serial_cmd_loop() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (s_len > 0) {
        s_buf[s_len] = '\0';
        handle_cmd(s_buf);
        s_len = 0;
      }
      continue;
    }
    if (s_len + 1 < kCmdMax) {
      s_buf[s_len++] = c;
    }
  }
}
