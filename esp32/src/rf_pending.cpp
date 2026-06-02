#include "rf_pending.h"

#ifdef FD_ENABLE_TDECK_UI

#include <Arduino.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "rf_sightings.h"

namespace {

enum RfPendingKind : uint8_t { kWifi = 0, kBle = 1 };

struct RfPendingRec {
  RfPendingKind kind;
  uint8_t mac[6];
  int rssi;
  uint8_t channel;
  uint16_t mfgrid;
  bool has_mfgrid;
  char name[32];
};

QueueHandle_t s_rf_queue = nullptr;

}  // namespace

void rf_pending_begin() {
  if (!s_rf_queue) {
    s_rf_queue = xQueueCreate(32, sizeof(RfPendingRec));
  }
}

// enqueue_rec is called from both WiFi task context (promisc_cb path via
// rf_pending_note_wifi) and BLE task context (rf_pending_note_ble).
// xQueueSend with timeout=0 is correct for both.
static void enqueue_rec(const RfPendingRec &rec) {
  if (!s_rf_queue) {
    return;
  }
  (void)xQueueSend(s_rf_queue, &rec, 0);
}

// Called from promisc_cb (WiFi task context) — IRAM_ATTR keeps the function
// resident in RAM when the flash cache is briefly stalled.
void IRAM_ATTR rf_pending_note_wifi(const uint8_t mac[6], int rssi,
                                    uint8_t channel, const char *ssid) {
  RfPendingRec rec{};
  rec.kind = kWifi;
  memcpy(rec.mac, mac, 6);
  rec.rssi = rssi;
  rec.channel = channel;
  if (ssid && ssid[0]) {
    strncpy(rec.name, ssid, sizeof(rec.name) - 1);
  }
  enqueue_rec(rec);
}

void rf_pending_note_ble(const uint8_t mac[6], const char *name, int rssi,
                         uint16_t mfgrid, bool has_mfgrid) {
  RfPendingRec rec{};
  rec.kind = kBle;
  memcpy(rec.mac, mac, 6);
  rec.rssi = rssi;
  rec.mfgrid = mfgrid;
  rec.has_mfgrid = has_mfgrid;
  if (name && name[0]) {
    strncpy(rec.name, name, sizeof(rec.name) - 1);
  }
  enqueue_rec(rec);
}

void rf_pending_drain() {
  if (!s_rf_queue) {
    return;
  }
  RfPendingRec rec;
  while (xQueueReceive(s_rf_queue, &rec, 0) == pdTRUE) {
    if (rec.kind == kWifi) {
      rf_sightings_note_wifi(rec.mac, rec.rssi, rec.channel,
                             rec.name[0] ? rec.name : nullptr);
    } else {
      rf_sightings_note_ble(rec.mac, rec.name[0] ? rec.name : nullptr, rec.rssi,
                            rec.mfgrid, rec.has_mfgrid);
    }
  }
}

#endif  // FD_ENABLE_TDECK_UI
