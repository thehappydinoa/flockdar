// gps.h — optional NMEA GPS tagging (enable with -DFD_ENABLE_GPS).
#pragma once

#include <stddef.h>

#include "config.h"

#ifdef FD_ENABLE_GPS
// Thread-safe fix snapshot (updated only from gps_loop on the main task).
struct GpsSnapshot {
  bool fix_valid;
  double lat;
  double lon;
  double alt;
  double accuracy;
  bool utc_valid;
  uint16_t utc_year;
  uint8_t utc_month;
  uint8_t utc_day;
  uint8_t utc_hour;
  uint8_t utc_minute;
  uint8_t utc_second;
};

struct GpsUtcTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

void gps_begin();
void gps_loop();
void gps_serial_status(bool force = false);
void gps_status(bool *fix, double *lat, double *lon, uint32_t *nmea_chars,
                uint8_t *sats);
bool gps_current(double *lat, double *lon, double *alt, double *accuracy);
// UTC date/time from the GPS fix (not local timezone).
bool gps_utc_now(GpsUtcTime *out);
// Local-time display: offset minutes added to UTC (persisted; default FD_TZ_OFFSET_MINUTES).
int16_t gps_tz_offset_min();
void gps_tz_set_offset_min(int16_t minutes);
void gps_format_local_time(bool has_utc, uint16_t year, uint8_t month, uint8_t day,
                           uint8_t hour, uint8_t minute, uint8_t second,
                           char *buf, size_t bufsz);
#endif
