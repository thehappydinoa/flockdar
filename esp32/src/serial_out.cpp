#include "serial_out.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "signing.h"

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
    else if (c < 0x20 || c >= 0x7f) {
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

// Single output path for every JSON line: USB serial and (if built) the SD
// card log. The line must NOT already contain a trailing newline.
static void output_line(const char *line) {
  Serial.write((const uint8_t *)line, strlen(line));
  Serial.write('\n');
#ifdef FD_ENABLE_SD
  sdlog_write(line);
#endif
}

void serial_out_begin() { Serial.begin(FD_SERIAL_BAUD); }

void serial_out_info(const char *msg) {
  char line[192];
  snprintf(line, sizeof(line),
           "{\"v\":%d,\"type\":\"info\",\"fw\":\"%s\",\"msg\":\"%s\","
           "\"ts_ms\":%lu}",
           FD_PROTO_VERSION, FD_FW_VERSION, msg, (unsigned long)millis());
  output_line(line);
}

#ifdef FD_ENABLE_GPS
void serial_out_gps_status(uint32_t nmea_chars, uint8_t sats, bool fix,
                           bool module) {
  char line[192];
  snprintf(line, sizeof(line),
           "{\"v\":%d,\"type\":\"gps_status\",\"fw\":\"%s\",\"nmea\":%lu,"
           "\"sats\":%u,\"fix\":%s,\"module\":%s,\"ts_ms\":%lu}",
           FD_PROTO_VERSION, FD_FW_VERSION, (unsigned long)nmea_chars,
           (unsigned)sats, fix ? "true" : "false", module ? "true" : "false",
           (unsigned long)millis());
  output_line(line);
}
#endif

void serial_out_emit(const Detection &d) {
  char body[448];  // headroom for escaped name + GPS fields
  int n = 0;

  if (d.kind == DET_GPS) {
    // GPS lines are unsigned (no sig field) per the protocol.
    snprintf(body, sizeof(body),
             "{\"v\":%d,\"type\":\"gps\",\"lat\":%.6f,\"lon\":%.6f,"
             "\"alt\":%.1f,\"accuracy\":%.1f,\"ts_ms\":%lu}",
             FD_PROTO_VERSION, d.lat, d.lon, d.alt, d.accuracy,
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

#if defined(FD_ENABLE_OLED) || defined(FD_ENABLE_TDECK_UI)
  display_note(d);
#endif
}
