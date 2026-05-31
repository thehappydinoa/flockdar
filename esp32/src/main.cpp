// flockdar-esp32 — passive Flock Safety camera detection.
//
// Two scanners run concurrently and push Detection records onto a shared
// queue: WiFi promiscuous mode (OUI match on addr1/addr2 + wildcard probe
// requests) and a NimBLE passive scan (name + manufacturer-ID match). The
// main loop drains the queue, signs each detection, and streams it as
// newline-delimited JSON over USB serial for the flockdar Python tool.
#include <Arduino.h>

#include "ble_scanner.h"
#include "config.h"
#include "protocol.h"
#include "serial_out.h"
#include "wifi_scanner.h"
#include "rf_sightings.h"

#ifdef FD_ENABLE_GPS
#include "gps.h"
#endif
#if defined(FD_ENABLE_OLED) || defined(FD_ENABLE_TDECK_UI)
#include "display.h"
#endif
#ifdef FD_ENABLE_SD
#include "sdlog.h"
#endif

QueueHandle_t g_det_queue = nullptr;

void setup() {
  serial_out_begin();
  delay(200);

  g_det_queue = xQueueCreate(64, sizeof(Detection));

#if defined(FD_ENABLE_OLED) || defined(FD_ENABLE_TDECK_UI)
  display_begin();
#endif
#ifdef FD_ENABLE_SD
  sdlog_begin();
#endif
#ifdef FD_ENABLE_GPS
  gps_begin();
#endif

  rf_sightings_begin();
  wifi_scanner_begin();
  ble_scanner_begin();

  serial_out_info("flockdar-esp32 online");
#ifdef FD_ENABLE_SD
  serial_out_info(sdlog_ok() ? "sd log open" : "sd card not found");
#endif
}

void loop() {
  // Drain everything the scanners produced since the last pass.
  Detection d;
  while (g_det_queue && xQueueReceive(g_det_queue, &d, 0) == pdTRUE) {
    serial_out_emit(d);
  }

  wifi_scanner_loop();  // channel hop
#ifdef FD_ENABLE_GPS
  gps_loop();
#endif
#if defined(FD_ENABLE_OLED) || defined(FD_ENABLE_TDECK_UI)
  display_loop();
#endif
#ifdef FD_ENABLE_SD
  sdlog_loop();
#endif

  delay(2);
}
