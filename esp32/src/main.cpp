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
#include "serial_cmd.h"
#include "stats.h"
#include "wifi_scanner.h"

#ifdef FD_ENABLE_TDECK_UI
#include "rf_pending.h"
#include "rf_sightings.h"
#endif

#ifdef FD_ENABLE_GPS
#include "gps.h"
#endif
#if defined(FD_ENABLE_OLED) || defined(FD_ENABLE_TDECK_UI)
#include "display.h"
#endif
#ifdef FD_ENABLE_SD
#include "sdlog.h"
#endif

#include "esp_task_wdt.h"

QueueHandle_t g_det_queue = nullptr;

void setup() {
  serial_out_begin();
  delay(200);

  stats_begin();
  g_det_queue = xQueueCreate(64, sizeof(Detection));
  if (!g_det_queue) {
    serial_out_info("fatal: detection queue alloc failed");
  }

#if defined(FD_ENABLE_OLED) || defined(FD_ENABLE_TDECK_UI)
  display_begin();
#endif
#ifdef FD_ENABLE_SD
  sdlog_begin();
#endif
#ifdef FD_ENABLE_GPS
  gps_begin();
#endif

#ifdef FD_ENABLE_TDECK_UI
  rf_sightings_begin();
  rf_pending_begin();
#endif

  if (g_det_queue) {
    wifi_scanner_begin();
    ble_scanner_begin();
  }

  {
    char msg[32];
    snprintf(msg, sizeof(msg), "online %s", FD_FW_VERSION);
    serial_out_info(msg);
  }
#ifdef FD_ENABLE_SD
  serial_out_info(sdlog_ok() ? "sd log open" : "sd card not found");
#endif
}

void loop() {
#ifdef FD_ENABLE_SD
  // SD dump/list owns the CPU + serial port — skip scanners so lines don't
  // interleave with the replay stream and throttle throughput.
  if (sdlog_host_busy()) {
    sdlog_dump_poll();
    serial_cmd_loop();
    return;
  }
#endif

  Detection d;
  for (int i = 0; i < FD_QUEUE_DRAIN_MAX; i++) {
    if (!g_det_queue || xQueueReceive(g_det_queue, &d, 0) != pdTRUE) {
      break;
    }
    serial_out_emit(d);
    yield();
    esp_task_wdt_reset();
  }

#ifdef FD_ENABLE_TDECK_UI
  rf_pending_drain();
#endif

  wifi_scanner_loop();  // channel hop

  // Handle host commands before periodic gps_status / SD work so responses
  // are not emitted back-to-back in the same loop pass.
  serial_cmd_loop();

#ifdef FD_ENABLE_GPS
  gps_loop();
#endif
#if defined(FD_ENABLE_OLED) || defined(FD_ENABLE_TDECK_UI)
  display_loop();
#endif
#ifdef FD_ENABLE_SD
  sdlog_dump_poll();
  sdlog_loop();
#endif

  stats_loop();

  delay(2);
}
