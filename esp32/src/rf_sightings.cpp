#include "rf_sightings.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#ifdef FD_ENABLE_GPS
#include "gps.h"
#endif

namespace {

constexpr size_t kMaxDevices = 48;

struct Entry {
  uint8_t mac[6];
  char kind[5];
  char label[33];
  int rssi;
  uint8_t channel;
  uint32_t seen;
  uint32_t last_ms;
  double lat;
  double lon;
  bool has_gps;
  bool has_utc;
  uint16_t utc_year;
  uint8_t utc_month;
  uint8_t utc_day;
  uint8_t utc_hour;
  uint8_t utc_minute;
  uint8_t utc_second;
  uint16_t mfgrid;
  bool has_mfgrid;
};

Entry s_devs[kMaxDevices];
Entry s_sorted[kMaxDevices];
size_t s_count = 0;
uint32_t s_events = 0;
bool s_sort_dirty = true;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

bool is_unicast(const uint8_t mac[6]) { return (mac[0] & 0x01) == 0; }

void mac_to_str(const uint8_t mac[6], char out[18]) {
  snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
}

int find_mac(const uint8_t mac[6]) {
  for (size_t i = 0; i < s_count; i++) {
    if (memcmp(s_devs[i].mac, mac, 6) == 0) return (int)i;
  }
  return -1;
}

int find_oldest_slot() {
  if (s_count < kMaxDevices) return (int)s_count;
  int idx = 0;
  for (size_t i = 1; i < kMaxDevices; i++) {
    if (s_devs[i].last_ms < s_devs[idx].last_ms) idx = (int)i;
  }
  return idx;
}

void note_device(const uint8_t mac[6], const char *kind, const char *label,
                 int rssi, uint8_t channel, uint16_t mfgrid = 0,
                 bool has_mfgrid = false) {
  portENTER_CRITICAL_ISR(&s_mux);
  s_events++;
  int idx = find_mac(mac);
  if (idx < 0) {
    idx = find_oldest_slot();
    if ((size_t)idx == s_count && s_count < kMaxDevices) s_count++;
    Entry &e = s_devs[idx];
    memcpy(e.mac, mac, 6);
    strncpy(e.kind, kind, sizeof(e.kind) - 1);
    strncpy(e.label, label ? label : "", sizeof(e.label) - 1);
    e.channel = channel;
    e.seen = 1;
    e.mfgrid = mfgrid;
    e.has_mfgrid = has_mfgrid;
  } else {
    Entry &e = s_devs[idx];
    e.seen++;
    if (label && label[0]) strncpy(e.label, label, sizeof(e.label) - 1);
    if (has_mfgrid) {
      e.mfgrid = mfgrid;
      e.has_mfgrid = true;
    }
  }
  s_devs[idx].rssi = rssi;
  s_devs[idx].last_ms = millis();
#ifdef FD_ENABLE_GPS
  double lat = 0.0;
  double lon = 0.0;
  double alt = 0.0;
  double accuracy = 0.0;
  s_devs[idx].has_gps = gps_current(&lat, &lon, &alt, &accuracy);
  if (s_devs[idx].has_gps) {
    s_devs[idx].lat = lat;
    s_devs[idx].lon = lon;
  }
  GpsUtcTime utc{};
  s_devs[idx].has_utc = gps_utc_now(&utc);
  if (s_devs[idx].has_utc) {
    s_devs[idx].utc_year = utc.year;
    s_devs[idx].utc_month = utc.month;
    s_devs[idx].utc_day = utc.day;
    s_devs[idx].utc_hour = utc.hour;
    s_devs[idx].utc_minute = utc.minute;
    s_devs[idx].utc_second = utc.second;
  }
#else
  s_devs[idx].has_gps = false;
  s_devs[idx].has_utc = false;
#endif
  s_sort_dirty = true;
  portEXIT_CRITICAL_ISR(&s_mux);
}

static int cmp_last_ms(const void *a, const void *b) {
  const Entry *ea = (const Entry *)a;
  const Entry *eb = (const Entry *)b;
  if (ea->last_ms > eb->last_ms) return -1;
  if (ea->last_ms < eb->last_ms) return 1;
  return 0;
}

void refresh_sorted() {
  // s_sort_dirty is written from ISR context (note_device) so read it under
  // the spinlock to avoid a torn read on multi-core.
  portENTER_CRITICAL_ISR(&s_mux);
  const bool dirty = s_sort_dirty;
  portEXIT_CRITICAL_ISR(&s_mux);
  if (!dirty) return;

  // Snapshot count + data while holding the lock (must be brief — we're
  // callable from a task context while the ISR may fire on the other core).
  // Use a local buffer so qsort runs entirely outside the spinlock.
  Entry tmp[kMaxDevices];
  portENTER_CRITICAL_ISR(&s_mux);
  const size_t n = s_count;
  if (n > 0) memcpy(tmp, s_devs, n * sizeof(Entry));
  s_sort_dirty = false;  // clear under lock so a concurrent ISR can re-set it
  portEXIT_CRITICAL_ISR(&s_mux);

  if (n > 1) {
    qsort(tmp, n, sizeof(Entry), cmp_last_ms);
  }

  // Write sorted snapshot back under the lock.
  portENTER_CRITICAL_ISR(&s_mux);
  memcpy(s_sorted, tmp, n * sizeof(Entry));
  portEXIT_CRITICAL_ISR(&s_mux);
}

}  // namespace

void rf_sightings_begin() {
  portENTER_CRITICAL_ISR(&s_mux);
  memset(s_devs, 0, sizeof(s_devs));
  memset(s_sorted, 0, sizeof(s_sorted));
  s_count = 0;
  s_events = 0;
  s_sort_dirty = true;
  portEXIT_CRITICAL_ISR(&s_mux);
}

void rf_sightings_note_wifi(const uint8_t mac[6], int rssi, uint8_t channel,
                            const char *ssid) {
  if (!is_unicast(mac)) return;
  note_device(mac, "wifi", ssid && ssid[0] ? ssid : "mgmt", rssi, channel);
}

void rf_sightings_note_ble(const uint8_t mac[6], const char *name, int rssi,
                           uint16_t mfgrid, bool has_mfgrid) {
  note_device(mac, "ble", name && name[0] ? name : "ble", rssi, 0, mfgrid,
              has_mfgrid);
}

size_t rf_sightings_count() {
  portENTER_CRITICAL_ISR(&s_mux);
  size_t n = s_count;
  portEXIT_CRITICAL_ISR(&s_mux);
  return n;
}

uint32_t rf_sightings_events() {
  portENTER_CRITICAL_ISR(&s_mux);
  uint32_t n = s_events;
  portEXIT_CRITICAL_ISR(&s_mux);
  return n;
}

bool rf_sightings_get(size_t index, RfDevice *out) {
  if (!out) return false;
  refresh_sorted();
  portENTER_CRITICAL_ISR(&s_mux);
  const size_t n = s_count;
  if (index >= n) {
    portEXIT_CRITICAL_ISR(&s_mux);
    return false;
  }
  const Entry &e = s_sorted[index];
  mac_to_str(e.mac, out->mac);
  memcpy(out->mac_raw, e.mac, 6);
  strncpy(out->kind, e.kind, sizeof(out->kind) - 1);
  strncpy(out->label, e.label, sizeof(out->label) - 1);
  out->rssi = e.rssi;
  out->channel = e.channel;
  out->seen = e.seen;
  out->seen_ms = e.last_ms;
  out->lat = e.lat;
  out->lon = e.lon;
  out->has_gps = e.has_gps;
  out->has_utc = e.has_utc;
  out->utc_year = e.utc_year;
  out->utc_month = e.utc_month;
  out->utc_day = e.utc_day;
  out->utc_hour = e.utc_hour;
  out->utc_minute = e.utc_minute;
  out->utc_second = e.utc_second;
  out->mfgrid = e.mfgrid;
  out->has_mfgrid = e.has_mfgrid;
  portEXIT_CRITICAL_ISR(&s_mux);
  return true;
}
