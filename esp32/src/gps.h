// gps.h — optional NMEA GPS tagging (enable with -DFD_ENABLE_GPS).
#pragma once

#include "config.h"

#ifdef FD_ENABLE_GPS
void gps_begin();
void gps_loop();
void gps_serial_status(bool force = false);
void gps_status(bool *fix, double *lat, double *lon, uint32_t *nmea_chars,
                uint8_t *sats);
bool gps_current(double *lat, double *lon, double *alt, double *accuracy);
#endif
