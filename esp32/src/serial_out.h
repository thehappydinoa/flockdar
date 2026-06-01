// serial_out.h — newline-delimited JSON output over USB serial.
#pragma once

#include "protocol.h"

void serial_out_begin();

// Serialise one detection to a signed (wifi/ble) or unsigned (gps) JSON line
// and print it. Called only from the main loop.
void serial_out_emit(const Detection &d);

// Emit a free-form info line (unsigned). Used for the boot banner.
void serial_out_info(const char *msg);

#ifdef FD_ENABLE_GPS
// Periodic GPS debug snapshot (unsigned). See README "gps_status" line type.
void serial_out_gps_status(uint32_t nmea_chars, uint8_t sats, bool fix,
                           bool module);
#endif
