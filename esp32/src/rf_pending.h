// rf_pending.h — defer nearby-RF sightings from radio callbacks to main loop.
// Only built with -DFD_ENABLE_TDECK_UI.
#pragma once

#include <stdint.h>

#ifdef FD_ENABLE_TDECK_UI

void rf_pending_begin();
void rf_pending_drain();

void rf_pending_note_wifi(const uint8_t mac[6], int rssi, uint8_t channel,
                          const char *ssid = nullptr);
void rf_pending_note_ble(const uint8_t mac[6], const char *name, int rssi,
                         uint16_t mfgrid, bool has_mfgrid);

#endif
