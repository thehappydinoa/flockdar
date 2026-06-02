#include "serial_out.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "match.h"
#include "signing.h"
#include "stats.h"

#include "freertos/FreeRTOS.h"

#include "esp_task_wdt.h"
#include "esp_timer.h"

#if defined(FD_ENABLE_OLED) || defined(FD_ENABLE_TDECK_UI)
#include "display.h"
#endif
#ifdef FD_ENABLE_SD
#include "sdlog.h"
#endif
#ifdef FD_ENABLE_GPS
#include "gps.h"
#endif

static void mac_to_str(const uint8_t mac[6], char out[18]) {
  snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
}

// Escape a (BLE-advertised, attacker-controllable) string for embedding in a
// JSON string literal. Produces pure-ASCII output so the result is always
// valid JSON regardless of quotes, backslashes, control chars, or non-UTF-8
// bytes in the source. Output is truncated to fit `outsz` (incl. NUL).
static void json_escape(const char *in, char *out, size_t outsz) {
  size_t o = 0;
  char u[7];
  for (size_t i = 0; in[i] != '\0'; i++) {
    unsigned char c = (unsigned char)in[i];
    const char *esc;
    if (c == '"')       esc = "\\\"";
    else if (c == '\\') esc = "\\\\";
    else if (c == '\n') esc = "\\n";
    else if (c == '\r') esc = "\\r";
    else if (c == '\t') esc = "\\t";
    else if (c < 0x20) {
      snprintf(u, sizeof(u), "\\u%04x", c);
      esc = u;
    } else {
      if (o + 1 >= outsz) break;
      out[o++] = (char)c;
      continue;
    }
    size_t len = strlen(esc);
    if (o + len >= outsz) break;
    memcpy(out + o, esc, len);
    o += len;
  }
  out[o] = '\0';
}

static portMUX_TYPE s_serial_mux = portMUX_INITIALIZER_UNLOCKED;

// All USB serial output must hold this lock so two Serial.write calls from
// different code paths cannot interleave (e.g. gps_status + stats response).
static void serial_usb_write(const uint8_t *data, size_t len) {
  if (len == 0) {
    return;
  }
  portENTER_CRITICAL(&s_serial_mux);
  Serial.write(data, len);
  portEXIT_CRITICAL(&s_serial_mux);
}

static void serial_usb_write_line(const char *line) {
  const size_t n = strlen(line);
  char buf[544];
  if (n + 1 <= sizeof(buf)) {
    if (n > 0) {
      memcpy(buf, line, n);
    }
    buf[n] = '\n';
    serial_usb_write((const uint8_t *)buf, n + 1);
    return;
  }
  portENTER_CRITICAL(&s_serial_mux);
  Serial.write((const uint8_t *)line, n);
  Serial.write('\n');
  portEXIT_CRITICAL(&s_serial_mux);
}

// Timeout for the USB FIFO drain loop: if the host is disconnected the CDC
// TX buffer fills and availableForWrite() returns 0 forever.  After this many
// microseconds we give up rather than hanging the task indefinitely.
static constexpr int64_t kUsbWriteTimeoutUs = 2000000LL;  // 2 seconds

void serial_out_usb_write(const uint8_t *data, size_t len) {
  size_t off = 0;
  const int64_t deadline = esp_timer_get_time() + kUsbWriteTimeoutUs;
  while (off < len) {
    if (esp_timer_get_time() > deadline) {
      // Host appears disconnected; abandon the write to avoid hanging the task.
      break;
    }
    int space = Serial.availableForWrite();
    if (space <= 0) {
      yield();
      esp_task_wdt_reset();
      continue;
    }
    size_t n = len - off;
    if ((size_t)space < n) {
      n = (size_t)space;
    }
    if (n > 128) {
      n = 128;
    }
    portENTER_CRITICAL(&s_serial_mux);
    Serial.write(data + off, n);
    portEXIT_CRITICAL(&s_serial_mux);
    off += n;
    if (off < len) {
      yield();
      esp_task_wdt_reset();
    }
  }
}

// Single output path for every JSON line: USB serial and (if built) the SD
// card log. The line must NOT already contain a trailing newline.
static void output_line(const char *line) {
  serial_usb_write_line(line);
#ifdef FD_ENABLE_SD
  sdlog_write(line);
#endif
}

void serial_out_begin() {
  Serial.begin(FD_SERIAL_BAUD);
  signing_begin();
}

void serial_out_raw(const char *line) { serial_usb_write_line(line); }

void serial_out_stats(const char *stats_json, size_t stats_len) {
  if (!stats_json) {
    return;
  }
  char line[384];
  const int w = snprintf(
      line, sizeof(line),
      "{\"v\":%d,\"type\":\"info\",\"fw\":\"%s\",\"msg\":\"stats\","
      "\"stats\":%.*s,\"ts_ms\":%lu}",
      FD_PROTO_VERSION, FD_FW_VERSION, (int)stats_len, stats_json,
      (unsigned long)millis());
  if (w < 0 || (size_t)w >= sizeof(line)) {
    serial_out_info("stats response truncated");
    return;
  }
  output_line(line);
}

void serial_out_info(const char *msg) {
  char esc[128];
  json_escape(msg, esc, sizeof(esc));
  char line[224];
  snprintf(line, sizeof(line),
           "{\"v\":%d,\"type\":\"info\",\"fw\":\"%s\",\"msg\":\"%s\","
           "\"ts_ms\":%lu}",
           FD_PROTO_VERSION, FD_FW_VERSION, esc, (unsigned long)millis());
  output_line(line);
}

#ifdef FD_ENABLE_GPS
void serial_out_gps_status(uint32_t nmea_chars, uint8_t sats, bool fix,
                           bool module, const char *chip) {
  char line[280];
  const int w = snprintf(
      line, sizeof(line),
      "{\"v\":%d,\"type\":\"gps_status\",\"fw\":\"%s\",\"nmea\":%lu,"
      "\"sats\":%u,\"fix\":%s,\"module\":%s,\"chip\":\"%s\","
      "\"ts_ms\":%lu}",
      FD_PROTO_VERSION, FD_FW_VERSION, (unsigned long)nmea_chars,
      (unsigned)sats, fix ? "true" : "false", module ? "true" : "false",
      chip ? chip : "unknown", (unsigned long)millis());
  if (w < 0 || (size_t)w >= sizeof(line)) {
    return;
  }
  output_line(line);
}
#endif

void serial_out_emit(const Detection &d) {
  char body[448];  // headroom for escaped name + GPS fields
  int n = 0;

  if (d.kind == DET_GPS) {
    // GPS lines are unsigned (no sig field) per the protocol.
    snprintf(body, sizeof(body),
             "{\"v\":%d,\"type\":\"gps\",\"fw\":\"%s\",\"lat\":%.6f,\"lon\":%.6f,"
             "\"alt\":%.1f,\"accuracy\":%.1f,\"ts_ms\":%lu}",
             FD_PROTO_VERSION, FD_FW_VERSION, d.lat, d.lon, d.alt, d.accuracy,
             (unsigned long)d.ts_ms);
    output_line(body);
    return;
  }

  const char *type = (d.kind == DET_WIFI) ? "wifi" : "ble";
  char mac[18];
  mac_to_str(d.mac, mac);

  n += snprintf(body + n, sizeof(body) - n,
                "{\"v\":%d,\"type\":\"%s\",\"method\":\"%s\",\"mac\":\"%s\"",
                FD_PROTO_VERSION, type, d.method, mac);
  if (d.emit_oui) {
    n += snprintf(body + n, sizeof(body) - n, ",\"oui\":\"%02x:%02x:%02x\"",
                  d.mac[0], d.mac[1], d.mac[2]);
  }
  if (d.has_name) {
    char esc[200];  // worst case: 31 src chars * 6 (\u00xx) + NUL
    json_escape(d.name, esc, sizeof(esc));
    n += snprintf(body + n, sizeof(body) - n, ",\"name\":\"%s\"", esc);
  }
  if (d.has_mfgrid) {
    n += snprintf(body + n, sizeof(body) - n, ",\"mfgrid\":%u", d.mfgrid);
  }
  if (d.has_rssi) {
    n += snprintf(body + n, sizeof(body) - n, ",\"rssi\":%d", d.rssi);
  }
  if (d.has_channel) {
    n += snprintf(body + n, sizeof(body) - n, ",\"channel\":%u", d.channel);
  }
  {
    char signal[24];
    char detail[128];
    char esc_detail[256];
    flock_match_labels(d.kind, d.method, d.mac, d.has_mac, d.name, d.has_name,
                       d.mfgrid, d.has_mfgrid, signal, sizeof(signal), detail,
                       sizeof(detail));
    if (signal[0]) {
      n += snprintf(body + n, sizeof(body) - n, ",\"signal\":\"%s\"", signal);
    }
    if (detail[0]) {
      json_escape(detail, esc_detail, sizeof(esc_detail));
      n += snprintf(body + n, sizeof(body) - n, ",\"detail\":\"%s\"", esc_detail);
    }
  }
#ifdef FD_ENABLE_GPS
  double lat = 0.0;
  double lon = 0.0;
  double alt = 0.0;
  double accuracy = 0.0;
  if (gps_current(&lat, &lon, &alt, &accuracy)) {
    n += snprintf(body + n, sizeof(body) - n, ",\"lat\":%.6f,\"lon\":%.6f",
                  lat, lon);
    if (accuracy > 0.0) {
      n += snprintf(body + n, sizeof(body) - n, ",\"accuracy\":%.1f", accuracy);
    }
  }
#endif
  n += snprintf(body + n, sizeof(body) - n, ",\"ts_ms\":%lu}",
                (unsigned long)d.ts_ms);

  // Sign the complete object (including the closing brace), then emit it with
  // the sig field inserted just before the brace. The receiver reconstructs
  // the signed bytes by stripping  ,"sig":"<hex>"  back to a closing brace.
  char sig[9];
  hmac_sig(body, (size_t)n, sig);

  char line[512];
  // %.*s copies body without its trailing '}'.
  snprintf(line, sizeof(line), "%.*s,\"sig\":\"%s\"}", n - 1, body, sig);
  output_line(line);
  stats_note_emit();

#if defined(FD_ENABLE_OLED) || defined(FD_ENABLE_TDECK_UI)
  display_note(d);
#endif
}
