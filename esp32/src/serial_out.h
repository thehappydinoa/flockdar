// serial_out.h — newline-delimited JSON output over USB serial.
#pragma once

#include "protocol.h"

void serial_out_begin();

// Serialise one detection to a signed (wifi/ble) or unsigned (gps) JSON line
// and print it. Called only from the main loop.
void serial_out_emit(const Detection &d);

// Emit a free-form info line (unsigned). Used for the boot banner.
void serial_out_info(const char *msg);
