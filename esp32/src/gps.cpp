#include "gps.h"

#ifdef FD_ENABLE_GPS

#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "protocol.h"

static TinyGPSPlus s_gps;
static HardwareSerial s_serial(FD_GPS_UART);
static uint32_t s_last_emit = 0;

#if defined(FD_BOARD_TDECK)
// L76K init (T-Deck Plus integrated GPS). Original T-Deck has no GPS module.
static bool tdeck_l76k_init() {
  s_serial.begin(FD_GPS_BAUD, SERIAL_8N1, FD_GPS_RX_PIN, FD_GPS_TX_PIN);
  for (int attempt = 0; attempt < 3; attempt++) {
    s_serial.write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
    delay(5);
    uint32_t drain_until = millis() + 500;
    while (millis() < drain_until) {
      while (s_serial.available()) {
        s_serial.read();
      }
      delay(1);
    }
    s_serial.flush();
    delay(100);
    s_serial.write("$PCAS06,0*1B\r\n");
    uint32_t wait = millis() + 500;
    while (!s_serial.available() && millis() < wait) {
      delay(1);
    }
    if (!s_serial.available()) {
      continue;
    }
    char ver[64];
    size_t n = 0;
    while (s_serial.available() && n + 1 < sizeof(ver)) {
      int c = s_serial.read();
      if (c < 0) break;
      if (c == '\n') break;
      ver[n++] = (char)c;
    }
    ver[n] = '\0';
    if (strncmp(ver, "$GPTXT,01,01,02", 15) == 0) {
      s_serial.write("$PCAS04,5*1C\r\n");
      delay(100);
      s_serial.write("$PCAS03,1,1,1,1,1,1,1,1,1,1,,,0,0*02\r\n");
      delay(100);
      s_serial.write("$PCAS11,3*1E\r\n");
      return true;
    }
    delay(200);
  }
  return false;
}
#endif

void gps_begin() {
#if defined(FD_BOARD_TDECK)
  if (!tdeck_l76k_init()) {
    // No L76K — leave UART open for an external module on the Grove port.
    s_serial.begin(FD_GPS_BAUD, SERIAL_8N1, FD_GPS_RX_PIN, FD_GPS_TX_PIN);
  }
#else
  s_serial.begin(FD_GPS_BAUD, SERIAL_8N1, FD_GPS_RX_PIN, FD_GPS_TX_PIN);
#endif
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
  d.accuracy = s_gps.hdop.isValid() ? s_gps.hdop.hdop() * 2.5 : 0.0;
  d.ts_ms = now;
  xQueueSend(g_det_queue, &d, 0);
}

void gps_status(bool *fix, double *lat, double *lon, uint32_t *nmea_chars,
                uint8_t *sats) {
  if (fix) {
    *fix = s_gps.location.isValid();
  }
  if (lat) {
    *lat = s_gps.location.isValid() ? s_gps.location.lat() : 0.0;
  }
  if (lon) {
    *lon = s_gps.location.isValid() ? s_gps.location.lng() : 0.0;
  }
  if (nmea_chars) {
    *nmea_chars = s_gps.charsProcessed();
  }
  if (sats) {
    *sats = s_gps.satellites.isValid() ? (uint8_t)s_gps.satellites.value() : 0;
  }
}

bool gps_current(double *lat, double *lon, double *alt, double *accuracy) {
  if (!s_gps.location.isValid()) {
    return false;
  }
  if (lat) {
    *lat = s_gps.location.lat();
  }
  if (lon) {
    *lon = s_gps.location.lng();
  }
  if (alt) {
    *alt = s_gps.altitude.isValid() ? s_gps.altitude.meters() : 0.0;
  }
  if (accuracy) {
    *accuracy = s_gps.hdop.isValid() ? s_gps.hdop.hdop() * 2.5 : 0.0;
  }
  return true;
}

#endif  // FD_ENABLE_GPS
