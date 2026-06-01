#include "gps.h"

#ifdef FD_ENABLE_GPS

#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "protocol.h"
#include "serial_out.h"

static TinyGPSPlus s_gps;
static HardwareSerial s_serial(FD_GPS_UART);
static uint32_t s_last_emit = 0;
static uint32_t s_last_status = 0;
static bool s_module_present = true;
static uint32_t s_uart_baud = FD_GPS_BAUD;

#if defined(FD_BOARD_TDECK)
enum GpsChip { kGpsUnknown, kGpsL76k, kGpsUblox };
static GpsChip s_chip = kGpsUnknown;

static const char *chip_label() {
  switch (s_chip) {
    case kGpsL76k:
      return "l76k";
    case kGpsUblox:
      return "ublox";
    default:
      return "unknown";
  }
}
#endif

#if defined(FD_BOARD_TDECK)
// T-Deck Plus: L76K @ 9600 or u-blox M10 @ 38400 (see esp32/BOARDS.md).

static void uart_drain(uint32_t ms) {
  const uint32_t end = millis() + ms;
  while (millis() < end) {
    while (s_serial.available()) {
      s_serial.read();
    }
    delay(1);
  }
}

static bool uart_read_line(char *out, size_t cap, uint32_t timeout_ms) {
  size_t n = 0;
  const uint32_t end = millis() + timeout_ms;
  while (millis() < end) {
    if (!s_serial.available()) {
      delay(1);
      continue;
    }
    const char c = (char)s_serial.read();
    if (c == '\n' || c == '\r') {
      if (n > 0) {
        out[n] = '\0';
        return true;
      }
      continue;
    }
    if (n + 1 < cap) {
      out[n++] = c;
    }
  }
  return false;
}

static bool line_is_l76k(const char *line) {
  return line && (strstr(line, "$GPTXT,01,01,02") != nullptr ||
                  strstr(line, "PCAS") != nullptr);
}

// u-blox answers invalid $PCAS with "$GNTXT,...,PCAS inv format" (Meshtastic logs).
static bool line_is_ublox_reject(const char *line) {
  return line && strstr(line, "inv format") != nullptr;
}

struct UartSniff {
  bool ubx_sync = false;
  bool nmea_dollar = false;
  uint32_t bytes = 0;
};

static UartSniff uart_sniff(uint32_t ms) {
  UartSniff out;
  uint8_t prev = 0;
  const uint32_t end = millis() + ms;
  while (millis() < end) {
    if (!s_serial.available()) {
      delay(1);
      continue;
    }
    const uint8_t c = (uint8_t)s_serial.read();
    out.bytes++;
    if (prev == 0xB5 && c == 0x62) {
      out.ubx_sync = true;
    }
    if (c == '$') {
      out.nmea_dollar = true;
    }
    prev = c;
  }
  return out;
}

static bool wait_for_position_nmea(uint32_t timeout_ms) {
  char line[96];
  const uint32_t end = millis() + timeout_ms;
  while (millis() < end) {
    const uint32_t slice = end - millis();
    if (slice == 0) {
      break;
    }
    if (!uart_read_line(line, sizeof(line), slice > 200 ? 200 : slice)) {
      continue;
    }
    if (strstr(line, "GGA") != nullptr || strstr(line, "RMC") != nullptr) {
      return true;
    }
  }
  return false;
}

static void l76k_configure() {
  s_serial.write("$PCAS10,3*1F\r\n");
  delay(1000);
  uart_drain(400);
  s_serial.write("$PCAS04,7*1E\r\n");
  delay(250);
  s_serial.write("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n");
  delay(250);
  s_serial.write("$PCAS11,3*1E\r\n");
  delay(250);
  s_serial.write("$PCAS02,1000*2E\r\n");
  delay(250);
  s_serial.flush();
}

static bool tdeck_try_l76k() {
#ifndef FD_GPS_UBLOX_ONLY
  s_serial.begin(9600, SERIAL_8N1, FD_GPS_RX_PIN, FD_GPS_TX_PIN);
  delay(100);
  uart_drain(300);

  // LilyGO: silence all sentences, then version probe.
  s_serial.write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
  delay(5);
  uart_drain(200);
  s_serial.write("$PCAS06,0*1B\r\n");

  char line[96];
  if (!uart_read_line(line, sizeof(line), 800)) {
    const UartSniff sniff = uart_sniff(250);
    if (sniff.ubx_sync) {
      return false;  // binary @9600 → try u-blox at 38400
    }
    return false;
  }
  if (line_is_ublox_reject(line)) {
    return false;
  }
  if (!line_is_l76k(line)) {
    return false;
  }

  l76k_configure();
  s_chip = kGpsL76k;
  s_uart_baud = 9600;
  serial_out_info("gps chip l76k");
  return true;
#else
  return false;
#endif
}

static uint8_t s_ubx_ack_buf[256];

static int ubx_wait_frame(uint8_t req_class, uint8_t req_id, uint32_t timeout_ms) {
  uint8_t state = 0;
  uint16_t need = 0;
  const uint32_t end = millis() + timeout_ms;
  while (millis() < end) {
    if (!s_serial.available()) {
      delay(1);
      continue;
    }
    const uint8_t c = (uint8_t)s_serial.read();
    switch (state) {
      case 0:
        state = (c == 0xB5) ? 1 : 0;
        break;
      case 1:
        state = (c == 0x62) ? 2 : 0;
        break;
      case 2:
        state = (c == req_class) ? 3 : 0;
        break;
      case 3:
        state = (c == req_id) ? 4 : 0;
        break;
      case 4:
        need = c;
        state = 5;
        break;
      case 5:
        need |= (uint16_t)c << 8;
        state = 6;
        break;
      case 6:
        if (need >= sizeof(s_ubx_ack_buf)) {
          state = 0;
          break;
        }
        if (s_serial.readBytes(s_ubx_ack_buf, need) == need) {
          return (int)need;
        }
        state = 0;
        break;
      default:
        state = 0;
        break;
    }
  }
  return 0;
}

// UBX-ACK-ACK (0x05/0x01). LilyGO GPSShield wrongly waits for CFG-RATE echo instead.
static int ubx_wait_ack_ack(uint32_t timeout_ms) {
  return ubx_wait_frame(0x05, 0x01, timeout_ms);
}

static void ubx_write(const uint8_t *msg, size_t len) {
  s_serial.write(msg, len);
}

static void ublox_recovery() {
  // LilyGO GPSShield.ino — CFG-CLEAR batches then CFG-RATE 1 Hz (best-effort).
  static const uint8_t kClear1[] = {
      0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x1C, 0xA2};
  static const uint8_t kClear2[] = {
      0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x1B, 0xA1};
  static const uint8_t kClear3[] = {
      0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x03, 0x1D, 0xB3};
  static const uint8_t kRate1Hz[] = {
      0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xE8, 0x03, 0x01,
      0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x3D};

  ubx_write(kClear1, sizeof(kClear1));
  ubx_wait_ack_ack(800);
  ubx_write(kClear2, sizeof(kClear2));
  ubx_wait_ack_ack(800);
  ubx_write(kClear3, sizeof(kClear3));
  ubx_wait_ack_ack(800);
  ubx_write(kRate1Hz, sizeof(kRate1Hz));
  ubx_wait_ack_ack(800);
}

static void ublox_enable_nmea() {
  static const uint8_t kMsgGga[] = {
      0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x00, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28};
  static const uint8_t kMsgRmc[] = {
      0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x04, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x4C};

  ubx_write(kMsgGga, sizeof(kMsgGga));
  ubx_wait_ack_ack(400);
  ubx_write(kMsgRmc, sizeof(kMsgRmc));
  ubx_wait_ack_ack(400);

  // PUBX: disable noisy sentences on UART, keep GGA + RMC at 1 Hz.
  const char *pubx[] = {
      "$PUBX,40,GLL,0,0,0,0,0,0*5C\r\n",
      "$PUBX,40,VTG,0,0,0,0,0,0*5E\r\n",
      "$PUBX,40,GSV,0,0,0,0,0,0*59\r\n",
      "$PUBX,40,GSA,0,0,0,0,0,0*4E\r\n",
      "$PUBX,40,GGA,0,1,0,0,0,0*5B\r\n",
      "$PUBX,40,RMC,0,1,0,0,0,0*46\r\n",
  };
  for (const char *cmd : pubx) {
    s_serial.write(cmd);
    delay(250);
  }
  s_serial.flush();
}

static bool tdeck_finish_ublox(uint32_t baud, bool nmea_ok) {
  s_chip = kGpsUblox;
  s_uart_baud = baud;
  s_module_present = true;
  char msg[56];
  snprintf(msg, sizeof(msg), "gps chip ublox @%lu%s",
           (unsigned long)baud, nmea_ok ? "" : " (warming)");
  serial_out_info(msg);
  return true;
}

static bool tdeck_try_ublox(uint32_t baud) {
  s_serial.begin(baud, SERIAL_8N1, FD_GPS_RX_PIN, FD_GPS_TX_PIN);
  // M10Q can be quiet for hundreds of ms after power-on; 38400 is factory default.
  delay(baud == 38400 ? 400 : 100);
  uart_drain(150);

  if (baud != 38400) {
    const UartSniff before = uart_sniff(200);
    if (!before.ubx_sync && !before.nmea_dollar && before.bytes < 4) {
      return false;
    }
  }

  ublox_recovery();
  ublox_enable_nmea();

  const bool nmea_ok = wait_for_position_nmea(3000);
  const UartSniff after = uart_sniff(300);
  if (!nmea_ok && !after.nmea_dollar && !after.ubx_sync && baud != 38400) {
    return false;
  }
  return tdeck_finish_ublox(baud, nmea_ok);
}

static bool tdeck_autobaud_init() {
  static const uint32_t kRates[] = {38400, 9600, 115200, 57600};
  for (uint32_t baud : kRates) {
    s_serial.begin(baud, SERIAL_8N1, FD_GPS_RX_PIN, FD_GPS_TX_PIN);
    delay(80);
    uart_drain(80);
    const UartSniff sniff = uart_sniff(400);
    if (!sniff.ubx_sync && !sniff.nmea_dollar) {
      continue;
    }
    if (sniff.ubx_sync || baud == 38400 || baud == 115200) {
      return tdeck_try_ublox(baud);
    }
    if (baud == 9600 && sniff.nmea_dollar) {
      s_serial.write("$PCAS06,0*1B\r\n");
      char line[96];
      if (uart_read_line(line, sizeof(line), 500) && line_is_l76k(line)) {
        l76k_configure();
        s_chip = kGpsL76k;
        s_uart_baud = 9600;
        serial_out_info("gps chip l76k (autobaud)");
        return true;
      }
      return tdeck_try_ublox(9600);
    }
  }
  return false;
}

static bool tdeck_gps_init() {
#ifndef FD_GPS_UBLOX_ONLY
  if (tdeck_try_l76k()) {
    return true;
  }
#endif
  if (tdeck_try_ublox(38400)) {
    return true;
  }
  if (tdeck_try_ublox(9600)) {
    return true;
  }
  if (tdeck_autobaud_init()) {
    return true;
  }
  return false;
}
#endif

void gps_begin() {
#if defined(FD_BOARD_TDECK)
  s_module_present = tdeck_gps_init();
#else
  s_serial.begin(FD_GPS_BAUD, SERIAL_8N1, FD_GPS_RX_PIN, FD_GPS_TX_PIN);
  s_uart_baud = FD_GPS_BAUD;
  s_module_present = true;
#endif
  gps_serial_status(true);
}

void gps_serial_status(bool force) {
  uint32_t now = millis();
  if (!force && now - s_last_status < FD_GPS_STATUS_INTERVAL_MS) {
    return;
  }
  s_last_status = now;

  bool fix = false;
  uint32_t nmea_chars = 0;
  uint8_t sats = 0;
  gps_status(&fix, nullptr, nullptr, &nmea_chars, &sats);
#if defined(FD_BOARD_TDECK)
  const char *chip = chip_label();
#else
  const char *chip = "generic";
#endif
  const bool module = s_module_present || nmea_chars >= 10;
  serial_out_gps_status(nmea_chars, sats, fix, module, chip);
}

static void gps_note_runtime_chip() {
#if defined(FD_BOARD_TDECK)
  if (s_chip != kGpsUnknown) {
    return;
  }
  if (s_gps.charsProcessed() < 50) {
    return;
  }
  s_chip = (s_uart_baud == 9600) ? kGpsL76k : kGpsUblox;
  s_module_present = true;
#endif
}

void gps_loop() {
  while (s_serial.available()) {
    s_gps.encode((char)s_serial.read());
  }

  gps_note_runtime_chip();
  gps_serial_status(false);

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
