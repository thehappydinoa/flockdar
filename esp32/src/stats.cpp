#include "stats.h"

#include <Arduino.h>
#include <stdio.h>

#include "ble_scanner.h"
#include "config.h"
#include "rf_sightings.h"
#include "wifi_scanner.h"

static volatile uint32_t s_queue_drops = 0;
static volatile uint32_t s_emits = 0;
static uint32_t s_last_heap_sample = 0;

void stats_begin() {
  s_queue_drops = 0;
  s_emits = 0;
  s_last_heap_sample = 0;
}

void stats_loop() {
  const uint32_t now = millis();
  if (now - s_last_heap_sample < 1000) {
    return;
  }
  s_last_heap_sample = now;
  (void)ESP.getMinFreeHeap();
}

void stats_note_queue_drop() { s_queue_drops++; }

void stats_note_emit() { s_emits++; }

bool stats_queue_send(const Detection &d) {
  if (!g_det_queue) {
    return false;
  }
  if (xQueueSend(g_det_queue, &d, 0) != pdTRUE) {
    stats_note_queue_drop();
    return false;
  }
  return true;
}

uint32_t stats_queue_drops() { return s_queue_drops; }
uint32_t stats_emits() { return s_emits; }
uint32_t stats_free_heap() { return ESP.getFreeHeap(); }
uint32_t stats_min_heap() { return ESP.getMinFreeHeap(); }

size_t stats_format_json(char *buf, size_t bufsz) {
  if (!buf || bufsz == 0) {
    return 0;
  }
#ifdef FD_ENABLE_TDECK_UI
  const uint32_t rf_events = rf_sightings_events();
#else
  const uint32_t rf_events = 0;
#endif
  return (size_t)snprintf(
      buf, bufsz,
      "{\"queue_drops\":%lu,\"emits\":%lu,\"wifi_mgmt\":%lu,\"ble_adverts\":%lu,"
      "\"rf_events\":%lu,\"free_heap\":%lu,\"min_heap\":%lu}",
      (unsigned long)s_queue_drops, (unsigned long)s_emits,
      (unsigned long)wifi_scanner_mgmt_frames(),
      (unsigned long)ble_scanner_adverts(), (unsigned long)rf_events,
      (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap());
}
