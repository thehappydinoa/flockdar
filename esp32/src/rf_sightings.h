// rf_sightings.h — deduplicated nearby WiFi/BLE devices (not Flock-filtered).
#pragma once

#include <stddef.h>
#include <stdint.h>

struct RfDevice {
  char mac[18];
  uint8_t mac_raw[6];
  char kind[5];   // "wifi" | "ble"
  char label[33]; // BLE name or WiFi SSID (max 32 chars + NUL)
  int rssi;
  uint8_t channel;
  uint32_t seen;
  uint32_t seen_ms;  // millis() at last sighting
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

void rf_sightings_begin();
void rf_sightings_note_wifi(const uint8_t mac[6], int rssi, uint8_t channel,
                            const char *ssid = nullptr);
void rf_sightings_note_ble(const uint8_t mac[6], const char *name, int rssi,
                           uint16_t mfgrid = 0, bool has_mfgrid = false);

size_t rf_sightings_count();
uint32_t rf_sightings_events();
bool rf_sightings_get(size_t index, RfDevice *out);
