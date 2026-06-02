#include "stats.h"

#include <Arduino.h>
#include <stdio.h>

#include "ble_scanner.h"
#include "config.h"
#include "rf_sightings.h"
#include "serial_out.h"
#include "wifi_scanner.h"

static volatile uint32_t s_queue_drops = 0;
static volatile uint32_t s_emits = 0;
static uint32_t s_last_heap_sample = 0;
static uint32_t s_last_heartbeat = 0;

void stats_begin() {
  s_queue_drops = 0;
  s_emits = 0;
  s_last_heap_sample = 0;
  s_last_heartbeat = 0;
}

void stats_loop() {
  const uint32_t now = millis();
  if (now - s_last_heap_sample < 1000) {
    return;
  }
  s_last_heap_sample = now;
  (void)ESP.getMinFreeHeap();

#if FD_STATS_HEARTBEAT_MS > 0
  if (now - s_last_heartbeat >= FD_STATS_HEARTBEAT_MS) {
    s_last_heartbeat = now;
    char body[256];
    const size_t n = stats_format_json(body, sizeof(body));
    serial_out_stats(body, n);
  }
#endif
}

void IRAM_ATTR stats_note_queue_drop() { s_queue_drops++; }

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

uint32_t stats_heap_total() { return ESP.getHeapSize(); }

uint32_t stats_heap_used() {
  const uint32_t total = ESP.getHeapSize();
  const uint32_t free = ESP.getFreeHeap();
  return total > free ? total - free : 0;
}

unsigned stats_heap_used_percent() {
  const uint32_t total = ESP.getHeapSize();
  if (total == 0) {
    return 0;
  }
  return (unsigned)((stats_heap_used() * 100ULL) / total);
}

void stats_format_ram(char *out, size_t outsz) {
  if (!out || outsz == 0) {
    return;
  }
  const uint32_t used = stats_heap_used();
  const unsigned pct = stats_heap_used_percent();
  const float mb = used / (1024.0f * 1024.0f);
  snprintf(out, outsz, "%.2f MB (%u%%)", mb, pct);
}

size_t stats_format_json(char *buf, size_t bufsz) {
  if (!buf || bufsz == 0) {
    return 0;
  }
#ifdef FD_ENABLE_TDECK_UI
  const uint32_t rf_events = rf_sightings_events();
#else
  const uint32_t rf_events = 0;
#endif
  char ram[32];
  stats_format_ram(ram, sizeof(ram));
  return (size_t)snprintf(
      buf, bufsz,
      "{\"queue_drops\":%lu,\"emits\":%lu,\"wifi_mgmt\":%lu,\"ble_adverts\":%lu,"
      "\"rf_events\":%lu,\"heap_total\":%lu,\"heap_used\":%lu,\"heap_used_pct\":%u,"
      "\"ram\":\"%s\"}",
      (unsigned long)s_queue_drops, (unsigned long)s_emits,
      (unsigned long)wifi_scanner_mgmt_frames(),
      (unsigned long)ble_scanner_adverts(), (unsigned long)rf_events,
      (unsigned long)stats_heap_total(), (unsigned long)stats_heap_used(),
      stats_heap_used_percent(), ram);
}
