#include "gps_track.h"
#ifdef FD_ENABLE_GPS

#include <Arduino.h>
#include "gps.h"

namespace {

constexpr uint32_t kSampleIntervalMs = 10000;  // sample every 10 s

TrackPoint s_buf[kMaxTrackPoints];
size_t s_head  = 0;  // index of next-to-write slot
size_t s_count = 0;  // total filled slots (0..kMaxTrackPoints)
uint32_t s_last_sample_ms = 0;
uint32_t s_events = 0;

}  // namespace

void gps_track_begin() {
  s_head  = 0;
  s_count = 0;
  s_last_sample_ms = 0;
  s_events = 0;
}

void gps_track_loop() {
  const uint32_t now = millis();
  if (now - s_last_sample_ms < kSampleIntervalMs) return;
  s_last_sample_ms = now;

  double lat = 0, lon = 0, alt = 0, acc = 0;
  if (!gps_current(&lat, &lon, &alt, &acc)) return;

  s_buf[s_head] = { (float)lat, (float)lon };
  s_head = (s_head + 1) % kMaxTrackPoints;
  if (s_count < kMaxTrackPoints) s_count++;
  s_events++;
}

size_t gps_track_count() { return s_count; }

uint32_t gps_track_events() { return s_events; }

bool gps_track_get(size_t index, TrackPoint *out) {
  if (!out || index >= s_count) return false;
  // index 0 = oldest; if buffer wrapped, oldest is at s_head
  size_t actual;
  if (s_count < kMaxTrackPoints) {
    actual = index;
  } else {
    actual = (s_head + index) % kMaxTrackPoints;
  }
  *out = s_buf[actual];
  return true;
}

bool gps_track_latest(TrackPoint *out) {
  if (s_count == 0) return false;
  size_t last = (s_head == 0) ? kMaxTrackPoints - 1 : s_head - 1;
  *out = s_buf[last];
  return true;
}

void gps_track_bounds(float *min_lat, float *max_lat,
                      float *min_lon, float *max_lon) {
  if (s_count == 0) {
    *min_lat = *max_lat = *min_lon = *max_lon = 0.0f;
    return;
  }
  TrackPoint first{};
  gps_track_get(0, &first);
  float mnlat = first.lat, mxlat = first.lat;
  float mnlon = first.lon, mxlon = first.lon;
  for (size_t i = 1; i < s_count; i++) {
    TrackPoint p{};
    if (!gps_track_get(i, &p)) continue;
    if (p.lat < mnlat) mnlat = p.lat;
    if (p.lat > mxlat) mxlat = p.lat;
    if (p.lon < mnlon) mnlon = p.lon;
    if (p.lon > mxlon) mxlon = p.lon;
  }
  *min_lat = mnlat; *max_lat = mxlat;
  *min_lon = mnlon; *max_lon = mxlon;
}

void gps_track_clear() {
  s_head = 0; s_count = 0; s_events = 0;
}

#endif  // FD_ENABLE_GPS
