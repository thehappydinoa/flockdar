// rf_sightings.h — deduplicated nearby WiFi/BLE devices (not Flock-filtered).
#pragma once

#include <stddef.h>
#include <stdint.h>

struct RfDevice {
  char mac[18];
  uint8_t mac_raw[6];
  char kind[5];   // "wifi" | "ble"
  char label[24]; // BLE name or WiFi subtype hint
  int rssi;
  uint8_t channel;
  uint32_t seen;
  uint16_t mfgrid;
  bool has_mfgrid;
};

void rf_sightings_begin();
void rf_sightings_note_wifi(const uint8_t mac[6], int rssi, uint8_t channel);
void rf_sightings_note_ble(const uint8_t mac[6], const char *name, int rssi,
                           uint16_t mfgrid = 0, bool has_mfgrid = false);

size_t rf_sightings_count();
uint32_t rf_sightings_events();
bool rf_sightings_get(size_t index, RfDevice *out);
