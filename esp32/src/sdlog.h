// sdlog.h — optional microSD logging (enable with -DFD_ENABLE_SD).
//
// Writes the exact same newline-delimited JSON the firmware streams over
// serial to a file on an SPI microSD card, so the device can wardrive
// untethered. Replay the log later with:
//   uv run flockdar-ingest flock-0001.ndjson out.sqlite
#pragma once

#include "config.h"

#ifdef FD_ENABLE_SD

struct SdlogStatus {
  bool mounted;
  bool logging;
  uint8_t card_type;
  uint32_t mount_attempts;
  uint32_t last_speed_hz;
  int miso_level;  // MISO pin sample at last mount (-1 if unknown)
  char card_type_name[8];
  char last_err[24];
  char path[24];
};

void sdlog_begin();
bool sdlog_ok();
const char *sdlog_path();
void sdlog_write(const char *line);
void sdlog_loop();
void sdlog_get_status(SdlogStatus *out);
// Force an immediate remount attempt (UI / debug).
bool sdlog_retry_mount();
// List /flock-*.ndjson on the card (unsigned info lines over serial).
void sdlog_list();
// Stream a log file over USB serial (raw NDJSON lines; not mirrored to SD).
// path: "/flock-0001.ndjson", "last" (previous run), or nullptr (current log).
bool sdlog_dump(const char *path);
// Drive an in-progress dump (call from main loop); returns true while active.
bool sdlog_dump_poll();
void sdlog_dump_abort();
bool sdlog_host_busy();

#endif
