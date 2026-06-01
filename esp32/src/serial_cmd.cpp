#include "serial_cmd.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

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
