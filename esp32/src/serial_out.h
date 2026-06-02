// serial_out.h — newline-delimited JSON output over USB serial.
#pragma once

#include "protocol.h"

void serial_out_begin();

// Serialise one detection to a signed (wifi/ble) or unsigned (gps) JSON line
// and print it. Called only from the main loop.
void serial_out_emit(const Detection &d);

// Emit a free-form info line (unsigned). Used for the boot banner.
void serial_out_info(const char *msg);

// USB serial only — does not mirror to the SD log (used for sd dump replay).
void serial_out_raw(const char *line);

// Emit stats JSON object (from stats_format_json) as an info line.
void serial_out_stats(const char *stats_json, size_t stats_len);

// Locked USB write for multi-line batches (SD dump replay). Caller must not
// interleave unrelated writers without this lock.
void serial_out_usb_write(const uint8_t *data, size_t len);

#ifdef FD_ENABLE_GPS
// Periodic GPS snapshot for host tools (flockdar-ingest --monitor).
void serial_out_gps_status(uint32_t nmea_chars, uint8_t sats, bool fix,
                           bool module, const char *chip);
#endif
