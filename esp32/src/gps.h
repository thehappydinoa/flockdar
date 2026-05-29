// gps.h — optional NMEA GPS tagging (enable with -DFD_ENABLE_GPS).
#pragma once

#include "config.h"

#ifdef FD_ENABLE_GPS
void gps_begin();
// Feed serial bytes to the parser and enqueue a periodic fix. Call from loop.
void gps_loop();
#endif
