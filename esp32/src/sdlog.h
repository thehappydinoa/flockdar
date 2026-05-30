// sdlog.h — optional microSD logging (enable with -DFD_ENABLE_SD).
//
// Writes the exact same newline-delimited JSON the firmware streams over
// serial to a file on an SPI microSD card, so the device can wardrive
// untethered. Replay the log later with:
//   uv run flockdar-ingest flock-0001.ndjson out.sqlite
#pragma once

#include "config.h"

#ifdef FD_ENABLE_SD
// Mounts the card and opens the next free /flock-NNNN.ndjson file.
void sdlog_begin();
// True if the card mounted and a log file is open.
bool sdlog_ok();
// Name of the open log file (or "" if none).
const char *sdlog_path();
// Append one line (no trailing newline). Buffered; periodically flushed.
void sdlog_write(const char *line);
// Flush the file at the configured interval. Call from loop.
void sdlog_loop();
#endif
