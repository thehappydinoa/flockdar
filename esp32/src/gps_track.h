// gps_track.h — circular GPS track buffer for the GPS Track app.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef FD_ENABLE_GPS

struct TrackPoint {
  float lat;
  float lon;
};

constexpr size_t kMaxTrackPoints = 512;

void gps_track_begin();
void gps_track_loop();   // call from main loop — samples position periodically

size_t gps_track_count();
// index 0 = oldest, count-1 = newest
bool gps_track_get(size_t index, TrackPoint *out);
bool gps_track_latest(TrackPoint *out);
void gps_track_bounds(float *min_lat, float *max_lat,
                      float *min_lon, float *max_lon);
void gps_track_clear();
uint32_t gps_track_events();  // increments when a point is added

#endif  // FD_ENABLE_GPS
