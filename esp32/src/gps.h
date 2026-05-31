// gps.h — optional NMEA GPS tagging (enable with -DFD_ENABLE_GPS).
#pragma once

#include "config.h"

#ifdef FD_ENABLE_GPS
void gps_begin();
void gps_loop();
// Snapshot for UI (fix, NMEA activity, satellites).
void gps_status(bool *fix, double *lat, double *lon, uint32_t *nmea_chars,
                uint8_t *sats);
#endif
