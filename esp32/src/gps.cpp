#include "gps.h"

#ifdef FD_ENABLE_GPS

#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "protocol.h"

static TinyGPSPlus s_gps;
static HardwareSerial s_serial(FD_GPS_UART);
static uint32_t s_last_emit = 0;

void gps_begin() {
  s_serial.begin(FD_GPS_BAUD, SERIAL_8N1, FD_GPS_RX_PIN, FD_GPS_TX_PIN);
}

void gps_loop() {
  while (s_serial.available()) {
    s_gps.encode(s_serial.read());
  }

  uint32_t now = millis();
  if (now - s_last_emit < FD_GPS_EMIT_INTERVAL_MS) return;
  if (!s_gps.location.isValid()) return;
  s_last_emit = now;

  if (!g_det_queue) return;
  Detection d;
  det_init(d, DET_GPS);
  d.lat = s_gps.location.lat();
  d.lon = s_gps.location.lng();
  d.alt = s_gps.altitude.isValid() ? s_gps.altitude.meters() : 0.0;
  // Rough horizontal accuracy estimate from HDOP (metres).
  d.accuracy = s_gps.hdop.isValid() ? s_gps.hdop.hdop() * 2.5 : 0.0;
  d.ts_ms = now;
  xQueueSend(g_det_queue, &d, 0);
}

#endif  // FD_ENABLE_GPS
