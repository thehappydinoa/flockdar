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
//
// Configures the L76K to acquire a fix worldwide. Commands match Meshtastic's
// L76K sequence (src/gps/GPS.cpp):
//   PCAS04,7  GPS + GLONASS + BeiDou (worldwide; was ,5 = GPS+GLONASS, which
//             dropped BeiDou and slowed time-to-fix outside its coverage)
//   PCAS03    output only RMC + GGA — at 9600 baud, enabling all 10 sentence
//             types floods the link and can starve the position sentences
//   PCAS11,3  vehicle navigation mode
//
// IMPORTANT: the config is sent UNCONDITIONALLY. The previous version gated the
// enable commands behind an exact "$GPTXT,01,01,02" version reply; when that
// probe didn't match (firmware-rev difference or a fragmented read) the module
// was never told to emit position sentences, so it produced NMEA chatter but
// never a fix — the classic "stuck at acquiring". The version probe is kept
// only to drain boot noise and confirm the module is present.
static bool tdeck_l76k_init() {
  s_serial.begin(FD_GPS_BAUD, SERIAL_8N1, FD_GPS_RX_PIN, FD_GPS_TX_PIN);
  delay(100);

  // Drain power-on banner / boot noise so the parser starts clean.
  uint32_t drain_until = millis() + 300;
  bool saw_any = false;
  while (millis() < drain_until) {
    while (s_serial.available()) {
      s_serial.read();
      saw_any = true;
    }
    delay(1);
  }

  // Probe for a reply so we can report module presence (does NOT gate config).
  s_serial.write("$PCAS06,0*1B\r\n");
  uint32_t wait = millis() + 500;
  while (!s_serial.available() && millis() < wait) {
    delay(1);
  }
  bool present = saw_any || s_serial.available();
  // Drain the probe response.
  uint32_t resp_until = millis() + 100;
  while (millis() < resp_until) {
    while (s_serial.available()) s_serial.read();
    delay(1);
  }

  // Apply configuration regardless of the probe outcome.
  s_serial.write("$PCAS04,7*1E\r\n");                       // GPS+GLONASS+BeiDou
  delay(100);
  s_serial.write("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n");  // RMC + GGA only
  delay(100);
  s_serial.write("$PCAS11,3*1E\r\n");                       // vehicle mode
  delay(100);
  s_serial.flush();
  return present;
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
