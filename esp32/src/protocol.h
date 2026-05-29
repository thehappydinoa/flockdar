// protocol.h — the Detection record passed between scanner tasks and the
// main loop, plus the shared queue.
//
// WiFi (promiscuous callback) and BLE (NimBLE) detections are produced in
// their own FreeRTOS task contexts. Rather than touch Serial / the OLED from
// those contexts, scanners fill a Detection and push it onto g_det_queue; the
// main loop drains the queue and does all serialisation and signing. This
// keeps the radio callbacks short and all output single-threaded.
#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

enum DetKind : uint8_t {
  DET_WIFI = 0,
  DET_BLE = 1,
  DET_GPS = 2,
};

struct Detection {
  DetKind kind;

  // method: "probe_request" | "addr1" | "addr2" | "name_match" | "mfgrid"
  char method[16];

  uint8_t mac[6];
  bool has_mac;
  bool emit_oui;  // WiFi: include the "oui" field derived from mac

  int rssi;
  bool has_rssi;

  uint8_t channel;
  bool has_channel;

  char name[32];  // BLE advertised name (NUL-terminated, truncated)
  bool has_name;

  uint16_t mfgrid;
  bool has_mfgrid;

  // GPS payload
  double lat, lon, alt, accuracy;

  uint32_t ts_ms;  // millis() at capture
};

// Created in setup(); shared by all scanner modules.
extern QueueHandle_t g_det_queue;

// Convenience: zero a Detection and stamp it with the current time.
static inline void det_init(Detection &d, DetKind kind) {
  d = Detection{};
  d.kind = kind;
}
