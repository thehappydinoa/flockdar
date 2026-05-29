#include "serial_out.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "signing.h"

#ifdef FD_ENABLE_OLED
#include "display.h"
#endif
#ifdef FD_ENABLE_SD
#include "sdlog.h"
#endif

static void mac_to_str(const uint8_t mac[6], char out[18]) {
  snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
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
  char line[160];
  snprintf(line, sizeof(line),
           "{\"v\":%d,\"type\":\"info\",\"msg\":\"%s\",\"ts_ms\":%lu}",
           FD_PROTO_VERSION, msg, (unsigned long)millis());
  output_line(line);
}

void serial_out_emit(const Detection &d) {
  char body[256];
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
    n += snprintf(body + n, sizeof(body) - n, ",\"name\":\"%s\"", d.name);
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
  n += snprintf(body + n, sizeof(body) - n, ",\"ts_ms\":%lu}",
                (unsigned long)d.ts_ms);

  // Sign the complete object (including the closing brace), then emit it with
  // the sig field inserted just before the brace. The receiver reconstructs
  // the signed bytes by stripping  ,"sig":"<hex>"  back to a closing brace.
  char sig[9];
  hmac_sig(body, (size_t)n, sig);

  char line[288];
  // %.*s copies body without its trailing '}'.
  snprintf(line, sizeof(line), "%.*s,\"sig\":\"%s\"}", n - 1, body, sig);
  output_line(line);

#ifdef FD_ENABLE_OLED
  display_note(d);
#endif
}
