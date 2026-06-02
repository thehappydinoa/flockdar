#include "tdeck_ui.h"

#ifdef FD_ENABLE_TDECK_UI

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Wire.h>

#include "config.h"
#include "tdeck_board.h"
#include "tdeck_ui_draw.h"
#include "tdeck_theme.h"
#include "wifi_scanner.h"
#include "ble_scanner.h"
#ifdef FD_ENABLE_SD
#include "sdlog.h"
#endif
#include "rf_sightings.h"
#include "stats.h"
#include "match.h"
#include "tdeck_icons.h"
#include "serial_out.h"

#include "esp_task_wdt.h"

#ifdef FD_ENABLE_GPS
#include "gps.h"
#endif

#ifdef FD_ENABLE_SD
#include <SD.h>
#include "sdlog.h"
#endif

namespace {

constexpr size_t kMaxHits = 24;
constexpr uint32_t kHitRedrawMs = 300;
constexpr uint32_t kBatPollMs = 5000;
constexpr int kHdrH = TdeckChrome::kHdrH;
constexpr int kListTopY = TdeckTheme::kBodyTop + 4;

using namespace TdeckTheme;

enum class Screen : uint8_t {
  kStatus = 0,
  kList = 1,
  kDetail = 2,
  kHelp = 3,
  kNearby = 4,
};

constexpr size_t kCarouselPages = 3;

// Status field layout (y offsets from kStatContentY).
constexpr int kStatContentY = kBodyTop + 4;
constexpr int kStatBlockGap = 2;  // divider padding (compact)

struct StatusLayout {
  int y_det_label;
  int y_det0;
  int y_det1;
  int y_det2;
  int y_ch;
  int y_sys_label;
  int y_drops;
  int y_ram;
#ifdef FD_ENABLE_GPS
  int y_gps;
#endif
#ifdef FD_ENABLE_SD
  int y_sd;
#endif
  int y_fw;
  int content_h;
};

StatusLayout status_layout() {
  StatusLayout L{};
  int y = 0;
  L.y_det_label = y;
  y += kSectionH + 2;
  L.y_det0 = y;
  y += kFieldH;
  L.y_det1 = y;
  y += kFieldH;
  L.y_det2 = y;
  y += kFieldH;
  L.y_ch = y;
  y += kFieldH + kStatBlockGap + kStatBlockGap;
  L.y_sys_label = y;
  y += kSectionH + 2;
  L.y_drops = y;
  y += kFieldH;
  L.y_ram = y;
  y += kFieldH;
#ifdef FD_ENABLE_GPS
  L.y_gps = y;
  y += kFieldH;
#endif
#ifdef FD_ENABLE_SD
  L.y_sd = y;
  y += kFieldH;
#endif
  L.y_fw = y;
  y += kFieldH;
  L.content_h = y;
  return L;
}

int s_status_scroll = 0;

struct HitLine {
  char mac[18];
  uint8_t mac_raw[6];
  bool has_mac;
  char kind[5];
  char method[16];
  char name[32];
  int rssi;
  uint8_t channel;
  uint16_t mfgrid;
  bool has_name;
  bool has_channel;
  bool has_mfgrid;
  uint32_t ts_ms;
  double lat;
  double lon;
  bool has_gps;
  bool has_utc;
  uint16_t utc_year;
  uint8_t utc_month;
  uint8_t utc_day;
  uint8_t utc_hour;
  uint8_t utc_minute;
  uint8_t utc_second;
};

enum class DetailSource : uint8_t { kFlock = 0, kNearby = 1 };

class TdeckShotSprite : public TFT_eSprite {
 public:
  using TFT_eSprite::TFT_eSprite;
  int rowPitch() const { return _iwidth; }
};

TFT_eSPI tft;
TdeckChrome chrome(tft);
static TdeckShotSprite s_snap(&tft);
static TFT_eSPI *s_ui_draw = &tft;

static TFT_eSPI &disp() { return *s_ui_draw; }

static void ui_set_draw_target(TFT_eSPI &target) {
  if (s_ui_draw != &target) {
    // Chrome paint caches are per framebuffer; switching TFT <-> sprite must
    // repaint header, dots, and soft keys (screenshot used to skip them).
    chrome.invalidate_header();
  }
  s_ui_draw = &target;
  chrome.bind(target);
}

int status_body_h() {
  return disp().height() - kHdrH - kChromeBottom;
}

int status_max_scroll(const StatusLayout &L) {
  const int content_bottom = kStatContentY + L.content_h;
  const int visible_bottom = disp().height() - kChromeBottom;
  const int max_s = content_bottom - visible_bottom;
  return max_s > 0 ? max_s : 0;
}

int status_paint_y(int rel_y) {
  return kStatContentY + rel_y - s_status_scroll;
}

void trackball_note_vertical();
void trackball_schedule_page(int dir);
void poll_trackball_page_pending();

bool s_ok = false;
bool s_kb = false;

uint32_t s_hit_total = 0;
HitLine s_hits[kMaxHits];
size_t s_hit_count = 0;
size_t s_list_sel = 0;
size_t s_nearby_sel = 0;
Screen s_screen = Screen::kStatus;
Screen s_return_screen = Screen::kStatus;
DetailSource s_detail_source = DetailSource::kFlock;
Screen s_detail_return = Screen::kList;

uint32_t s_last_draw = 0;
bool s_dirty = true;
uint8_t s_brightness = 16;
uint32_t s_last_input_ms = 0;
bool s_display_asleep = false;
Screen s_painted = Screen::kStatus;
bool s_status_static = false;

uint32_t s_paint_hits = UINT32_MAX;
uint8_t s_paint_ch = 255;
uint32_t s_paint_wifi_rf = 0;
uint32_t s_paint_ble_rf = 0;
uint32_t s_paint_drops = UINT32_MAX;
unsigned s_paint_ram_pct = 255;
uint32_t s_paint_ram_used = UINT32_MAX;
bool s_paint_gps_fix = false;
bool s_paint_gps_nmea = false;  // nmea_chars >= 10 (not raw count — avoids constant redraw)
uint8_t s_paint_sats = 255;
bool s_paint_sd = false;
char s_paint_sd_path[24] = "";
char s_paint_fw[16] = "";
size_t s_paint_hit_count = SIZE_MAX;
char s_paint_last_mac[18] = "";
char s_paint_last_detail[48] = "";
size_t s_paint_list_sel = SIZE_MAX;
size_t s_paint_list_start = SIZE_MAX;
size_t s_paint_list_count = SIZE_MAX;
size_t s_paint_detail_sel = SIZE_MAX;
uint8_t s_paint_detail_mode = 255;
size_t s_paint_nearby_sel = SIZE_MAX;
size_t s_paint_nearby_start = SIZE_MAX;
size_t s_paint_nearby_count = SIZE_MAX;
uint16_t s_bat_mv = 0;
bool s_bat_usb = false;
uint32_t s_last_bat_poll = 0;
bool s_help_painted = false;
uint32_t s_help_key_block_until = 0;
int s_paint_status_scroll = -1;
uint32_t s_hit_alert_until = 0;
constexpr uint32_t kHitAlertMs = 700;

void invalidate_status_paint_cache() {
  s_paint_status_scroll = -1;
  s_paint_hits = UINT32_MAX;
  s_paint_ch = 255;
  s_paint_wifi_rf = UINT32_MAX;
  s_paint_ble_rf = UINT32_MAX;
  s_paint_drops = UINT32_MAX;
  s_paint_ram_pct = 255;
  s_paint_ram_used = UINT32_MAX;
  s_paint_gps_fix = !s_paint_gps_fix;
  s_paint_gps_nmea = !s_paint_gps_nmea;
  s_paint_sats = 255;
  s_paint_sd = !s_paint_sd;
  s_paint_sd_path[0] = '\0';
  s_paint_fw[0] = '\0';
  s_paint_hit_count = SIZE_MAX;
  s_paint_last_mac[0] = '\0';
  s_paint_last_detail[0] = '\0';
}

bool kb_poll_key(char *out);

void kb_drain() {
  char key = 0;
  while (kb_poll_key(&key)) {
  }
}

const char *screen_title(Screen s) {
  switch (s) {
  case Screen::kDetail:
    return "DETAIL";
  default:
    return "FLOCKDAR";
  }
}

void format_seen_at(bool has_utc, uint16_t year, uint8_t month, uint8_t day,
                    uint8_t hour, uint8_t minute, uint8_t second, char *buf,
                    size_t bufsz) {
#ifdef FD_ENABLE_GPS
  gps_format_local_time(has_utc, year, month, day, hour, minute, second, buf,
                        bufsz);
#else
  (void)has_utc;
  (void)year;
  (void)month;
  (void)day;
  (void)hour;
  (void)minute;
  (void)second;
  strncpy(buf, "no GPS", bufsz - 1);
  buf[bufsz - 1] = '\0';
#endif
}

void paint_location_section(int &y, bool has_gps, double lat, double lon,
                            bool has_utc, uint16_t utc_year, uint8_t utc_month,
                            uint8_t utc_day, uint8_t utc_hour,
                            uint8_t utc_minute, uint8_t utc_second) {
  char val[32];
  chrome.paint_section_label(y, "LOCATION");
  y += kSectionH + 2;
#ifdef FD_ENABLE_GPS
  if (has_gps) {
    snprintf(val, sizeof(val), "%.5f,%.5f", lat, lon);
    chrome.paint_field(y, "Position", val);
  } else {
    chrome.paint_field(y, "Position", "no fix");
  }
#else
  chrome.paint_field(y, "Position", "no GPS");
#endif
  y += kFieldH;
  format_seen_at(has_utc, utc_year, utc_month, utc_day, utc_hour, utc_minute,
                 utc_second, val, sizeof(val));
  chrome.paint_field(y, "Seen at", val);
  y += kFieldH;
}

static DetKind hit_line_kind(const HitLine &h) {
  return (strcmp(h.kind, "ble") == 0) ? DET_BLE : DET_WIFI;
}

void paint_match_why(int &y, DetKind kind, const char *method,
                     const uint8_t *mac, bool has_mac, const char *name,
                     bool has_name, uint16_t mfgrid, bool has_mfgrid) {
  char summary[96];
  flock_match_summary(kind, method, mac, has_mac, name, has_name, mfgrid,
                      has_mfgrid, summary, sizeof(summary));
  if (!summary[0]) {
    return;
  }
  chrome.paint_text(4, y, "Why", kFontLabel, kTextMuted, kBg);
  y += kFieldH;
  const int wrap_w = disp().width() - 8;
  y = chrome.paint_wrapped_text(4, y, wrap_w, summary, kFontLabel, kText, kBg,
                               2, kFieldH);
  y += 2;
}

void tdeck_set_brightness(uint8_t value) {
  static uint8_t level = 0;
  static const uint8_t steps = 16;
  if (value == 0) {
    digitalWrite(TDECK_BL_PIN, LOW);
    delay(3);
    level = 0;
    return;
  }
  if (level == 0) {
    digitalWrite(TDECK_BL_PIN, HIGH);
    level = steps;
    delayMicroseconds(30);
  }
  int from = steps - level;
  int to = steps - value;
  int num = (steps + to - from) % steps;
  for (int i = 0; i < num; i++) {
    digitalWrite(TDECK_BL_PIN, LOW);
    digitalWrite(TDECK_BL_PIN, HIGH);
  }
  level = value;
}

void ui_note_input() {
  s_last_input_ms = millis();
  if (s_display_asleep) {
    s_display_asleep = false;
    tdeck_set_brightness(s_brightness);
    s_dirty = true;
  }
}

void poll_display_sleep() {
#if FD_DISPLAY_SLEEP_MS > 0
  if (s_display_asleep) {
    return;
  }
  if (millis() - s_last_input_ms >= (uint32_t)FD_DISPLAY_SLEEP_MS) {
    s_display_asleep = true;
    tdeck_set_brightness(0);
  }
#endif
}

void spi_bus_idle() { tdeck_spi_idle(); }

const char *rf_vendor(const RfDevice &d) {
  if (d.has_mfgrid) {
    const char *v = ble_vendor_name(d.mfgrid);
    if (v) return v;
  }
  return oui_vendor_name(d.mac_raw);
}

void push_hit(const Detection &d) {
  if (d.kind != DET_WIFI && d.kind != DET_BLE) return;
  s_hit_total++;

  HitLine line{};
  line.has_mac = d.has_mac;
  if (d.has_mac) {
    memcpy(line.mac_raw, d.mac, 6);
    snprintf(line.mac, sizeof(line.mac), "%02x:%02x:%02x:%02x:%02x:%02x",
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
  } else {
    line.mac_raw[0] = line.mac_raw[1] = line.mac_raw[2] = 0;
    strncpy(line.mac, "--", sizeof(line.mac));
  }
  strncpy(line.kind, d.kind == DET_WIFI ? "wifi" : "ble", sizeof(line.kind));
  strncpy(line.method, d.method, sizeof(line.method));
  line.rssi = d.has_rssi ? d.rssi : 0;
  line.channel = d.has_channel ? d.channel : 0;
  line.has_channel = d.has_channel;
  line.mfgrid = d.has_mfgrid ? d.mfgrid : 0;
  line.has_mfgrid = d.has_mfgrid;
  line.has_name = d.has_name;
  if (d.has_name) {
    strncpy(line.name, d.name, sizeof(line.name) - 1);
  } else {
    line.name[0] = '\0';
  }
  line.ts_ms = d.ts_ms;
#ifdef FD_ENABLE_GPS
  double lat = 0.0;
  double lon = 0.0;
  double alt = 0.0;
  double accuracy = 0.0;
  line.has_gps = gps_current(&lat, &lon, &alt, &accuracy);
  if (line.has_gps) {
    line.lat = lat;
    line.lon = lon;
  }
  GpsUtcTime utc{};
  line.has_utc = gps_utc_now(&utc);
  if (line.has_utc) {
    line.utc_year = utc.year;
    line.utc_month = utc.month;
    line.utc_day = utc.day;
    line.utc_hour = utc.hour;
    line.utc_minute = utc.minute;
    line.utc_second = utc.second;
  }
#else
  line.has_gps = false;
  line.has_utc = false;
#endif

  if (s_hit_count < kMaxHits) {
    s_hits[s_hit_count++] = line;
  } else {
    memmove(&s_hits[0], &s_hits[1], (kMaxHits - 1) * sizeof(HitLine));
    s_hits[kMaxHits - 1] = line;
  }
  if (s_list_sel >= s_hit_count && s_hit_count > 0) {
    s_list_sel = s_hit_count - 1;
  }
  s_hit_alert_until = millis() + kHitAlertMs;
  chrome.invalidate_header();
  uint32_t now = millis();
  if (now - s_last_draw >= kHitRedrawMs) {
    s_dirty = true;
  }
}

void note_gps(const Detection &d) {
  (void)d;
  s_dirty = true;
}

uint16_t read_battery_mv() {
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogReadMilliVolts(TDECK_BAT_ADC);
  }
  return (uint16_t)((float)(sum / 8) * TDECK_BAT_ADC_MULT);
}

void poll_battery() {
  uint32_t now = millis();
  if (s_last_bat_poll != 0 && now - s_last_bat_poll < kBatPollMs) return;
  s_last_bat_poll = now;

  uint16_t mv = read_battery_mv();
  bool usb = mv > 4350;
  if (mv != s_bat_mv || usb != s_bat_usb) {
    s_bat_mv = mv;
    s_bat_usb = usb;
    s_dirty = true;
  }
}

void paint_status_static_labels(const StatusLayout &L) {
  chrome.paint_section_label(status_paint_y(L.y_det_label), "DETECTION");
  const int y_div =
      kStatContentY + L.y_ch + kFieldH + kStatBlockGap - s_status_scroll;
  chrome.paint_divider(y_div);
  chrome.paint_section_label(status_paint_y(L.y_sys_label), "SYSTEM");
}

void paint_status_dynamic(bool force) {
  const StatusLayout L = status_layout();
  char val[32];

  if (force || s_hit_total != s_paint_hits) {
    snprintf(val, sizeof(val), "%lu", (unsigned long)s_hit_total);
    chrome.paint_field_icon(status_paint_y(L.y_det0), StatusIcon::kFlock,
                            "Flock hits", val);
    s_paint_hits = s_hit_total;
  }

  uint8_t ch = wifi_scanner_channel();
  uint32_t wifi_rf = wifi_scanner_mgmt_frames();
  uint32_t ble_rf = ble_scanner_adverts();
  if (force || ch != s_paint_ch || wifi_rf != s_paint_wifi_rf ||
      ble_rf != s_paint_ble_rf) {
    snprintf(val, sizeof(val), "%lu", (unsigned long)wifi_rf);
    chrome.paint_field_icon(status_paint_y(L.y_det1), StatusIcon::kWifi,
                            "WiFi frames", val);
    snprintf(val, sizeof(val), "%lu", (unsigned long)ble_rf);
    chrome.paint_field_icon(status_paint_y(L.y_det2), StatusIcon::kBle,
                            "BLE adverts", val);
    snprintf(val, sizeof(val), "%u", (unsigned)ch);
    chrome.paint_field_icon(status_paint_y(L.y_ch), StatusIcon::kChannel,
                            "Channel", val);
    s_paint_ch = ch;
    s_paint_wifi_rf = wifi_rf;
    s_paint_ble_rf = ble_rf;
  }

  const uint32_t drops = stats_queue_drops();
  const uint32_t ram_used = stats_heap_used();
  const unsigned ram_pct = stats_heap_used_percent();
  if (force || drops != s_paint_drops || ram_used != s_paint_ram_used ||
      ram_pct != s_paint_ram_pct) {
    snprintf(val, sizeof(val), "%lu", (unsigned long)drops);
    chrome.paint_field_icon(status_paint_y(L.y_drops), StatusIcon::kWifi,
                            "Queue drops", val);
    stats_format_ram(val, sizeof(val));
    chrome.paint_field_icon(status_paint_y(L.y_ram), StatusIcon::kBle, "RAM",
                            val);
    s_paint_drops = drops;
    s_paint_ram_used = ram_used;
    s_paint_ram_pct = ram_pct;
  }

#ifdef FD_ENABLE_GPS
  bool gps_fix = false;
  double gps_lat = 0;
  double gps_lon = 0;
  uint32_t nmea_chars = 0;
  uint8_t gps_sats = 0;
  gps_status(&gps_fix, &gps_lat, &gps_lon, &nmea_chars, &gps_sats);
  const bool nmea_ok = nmea_chars >= 10;
  if (force || gps_fix != s_paint_gps_fix || nmea_ok != s_paint_gps_nmea ||
      gps_sats != s_paint_sats) {
    if (gps_fix) {
      snprintf(val, sizeof(val), "%.5f, %.5f", gps_lat, gps_lon);
    } else if (!nmea_ok) {
      strncpy(val, "no module", sizeof(val));
    } else {
      snprintf(val, sizeof(val), "acquiring (%u)", (unsigned)gps_sats);
    }
    chrome.paint_field_icon(status_paint_y(L.y_gps), StatusIcon::kGps, "GPS", val);
    s_paint_gps_fix = gps_fix;
    s_paint_gps_nmea = nmea_ok;
    s_paint_sats = gps_sats;
  }
#endif

#ifdef FD_ENABLE_SD
  bool sd = sdlog_ok();
  if (force || sd != s_paint_sd ||
      strcmp(s_paint_sd_path, sd ? sdlog_path() : "") != 0) {
    if (sd) {
      chrome.paint_field_icon(status_paint_y(L.y_sd), StatusIcon::kSd, "Storage",
                              sdlog_path());
      strncpy(s_paint_sd_path, sdlog_path(), sizeof(s_paint_sd_path) - 1);
    } else {
      chrome.paint_field_icon(status_paint_y(L.y_sd), StatusIcon::kSd, "Storage",
                              "not mounted");
      s_paint_sd_path[0] = '\0';
    }
    s_paint_sd = sd;
  }
#endif

  if (force || strcmp(s_paint_fw, FD_FW_VERSION) != 0) {
    char ver[20];
    snprintf(ver, sizeof(ver), "v%s", FD_FW_VERSION);
    chrome.paint_field_icon(status_paint_y(L.y_fw), StatusIcon::kChannel,
                            "Firmware", ver);
    strncpy(s_paint_fw, FD_FW_VERSION, sizeof(s_paint_fw) - 1);
    s_paint_fw[sizeof(s_paint_fw) - 1] = '\0';
  }

  s_paint_status_scroll = s_status_scroll;
}

void scroll_status(int delta) {
  trackball_note_vertical();
  const StatusLayout L = status_layout();
  const int max_s = status_max_scroll(L);
  if (max_s == 0) return;
  const int step = kFieldH;
  const int prev = s_status_scroll;
  if (delta < 0) {
    if (s_status_scroll <= 0) return;
    s_status_scroll -= step;
    if (s_status_scroll < 0) s_status_scroll = 0;
  } else {
    if (s_status_scroll >= max_s) return;
    s_status_scroll += step;
    if (s_status_scroll > max_s) s_status_scroll = max_s;
  }
  if (s_status_scroll == prev) return;
  chrome.clear_body();
  s_status_static = false;
  invalidate_status_paint_cache();
  s_dirty = true;
}

void paint_status(bool force_full) {
  if (force_full || s_painted != Screen::kStatus) {
    chrome.clear_body();
    s_status_static = false;
    s_status_scroll = 0;
    invalidate_status_paint_cache();
    s_painted = Screen::kStatus;
  }
  const StatusLayout L = status_layout();
  const int max_s = status_max_scroll(L);
  if (max_s == 0) {
    s_status_scroll = 0;
  } else if (s_status_scroll > max_s) {
    s_status_scroll = max_s;
    s_status_static = false;
    invalidate_status_paint_cache();
  }

  const int body_h = status_body_h();
  const bool scroll_dirty = (s_status_scroll != s_paint_status_scroll);
  const bool dynamic_force = force_full || scroll_dirty || !s_status_static;

  disp().setViewport(0, kHdrH, disp().width(), body_h, false);
  if (dynamic_force) {
    chrome.clear_body();
    s_status_static = false;
  }

  if (!s_status_static) {
    paint_status_static_labels(L);
    s_status_static = true;
  }
  paint_status_dynamic(dynamic_force);
  const int y_tail = status_paint_y(L.content_h);
  const int y_bot = disp().height() - kChromeBottom;
  if (y_tail < y_bot) {
    disp().fillRect(0, y_tail, disp().width(), y_bot - y_tail, kBg);
    chrome.invalidate_dots();
  }
  disp().resetViewport();
}

uint16_t confidence_color(uint8_t level) {
  if (level >= 3) return kFlock;
  if (level >= 2) return kAccent;
  return kTextMuted;
}

void flock_icon_row_fn(size_t index, TdeckChrome::IconRow *out) {
  if (!out || index >= s_hit_count) {
    if (out) {
      out->icon = static_cast<uint8_t>(DevIcon::kCamera);
      strncpy(out->line1, "--", sizeof(out->line1));
      out->line2[0] = '\0';
    }
    return;
  }
  const HitLine &h = s_hits[index];
  out->icon = static_cast<uint8_t>(DevIcon::kCamera);
  const char *vendor = h.has_mac ? oui_vendor_name(h.mac_raw) : nullptr;
  if (vendor) {
    snprintf(out->line1, sizeof(out->line1), "Flock · %s", vendor);
  } else {
    snprintf(out->line1, sizeof(out->line1), "%s", h.mac);
  }
  const uint8_t conf =
      flock_det_confidence(h.kind, h.method, h.name, h.has_name);
  const char *cl = flock_confidence_label(conf);
  if (flock_method_is_probe(h.method)) {
    snprintf(out->line2, sizeof(out->line2), "%s · PROBE · %d dBm", cl,
             h.rssi);
  } else {
    snprintf(out->line2, sizeof(out->line2), "%s · %s · %d dBm", cl,
             flock_method_short(h.method), h.rssi);
  }
}

void paint_list(bool force) {
  chrome.paint_icon_scroll_list(s_hit_count, &s_list_sel, &s_paint_list_sel,
                                &s_paint_list_start, &s_paint_list_count,
                                flock_icon_row_fn, kListTopY, force, true);
}
void nearby_icon_row_fn(size_t index, TdeckChrome::IconRow *out) {
  RfDevice d{};
  if (!out || !rf_sightings_get(index, &d)) {
    if (out) {
      out->icon = static_cast<uint8_t>(DevIcon::kUnknown);
      strncpy(out->line1, "--", sizeof(out->line1));
      out->line2[0] = '\0';
    }
    return;
  }
  const char *vendor = rf_vendor(d);
  const DevIcon icon = classify_rf_device(d, vendor);
  out->icon = static_cast<uint8_t>(icon);
  if (vendor) {
    snprintf(out->line1, sizeof(out->line1), "%s · %s", vendor,
             dev_icon_label(icon));
  } else {
    snprintf(out->line1, sizeof(out->line1), "%s · %s", d.mac,
             dev_icon_label(icon));
  }
  if (strcmp(d.kind, "wifi") == 0) {
    snprintf(out->line2, sizeof(out->line2), "ch%u  %d dBm  x%lu",
             (unsigned)d.channel, d.rssi, (unsigned long)d.seen);
  } else if (d.label[0] && strcmp(d.label, "ble") != 0) {
    snprintf(out->line2, sizeof(out->line2), "%s  %d dBm  x%lu", d.label,
             d.rssi, (unsigned long)d.seen);
  } else {
    snprintf(out->line2, sizeof(out->line2), "%s  %d dBm  x%lu", d.mac,
             d.rssi, (unsigned long)d.seen);
  }
}

void paint_nearby(bool force) {
  const size_t count = rf_sightings_count();
  chrome.paint_icon_scroll_list(count, &s_nearby_sel, &s_paint_nearby_sel,
                                &s_paint_nearby_start, &s_paint_nearby_count,
                                nearby_icon_row_fn, kListTopY, force);
}


void paint_detail(bool force) {
  const uint8_t mode =
      (s_detail_source == DetailSource::kNearby) ? 1U : 0U;
  const size_t sel =
      (mode == 1U) ? s_nearby_sel : s_list_sel;
  if (!force && s_painted == Screen::kDetail &&
      s_paint_detail_mode == mode && sel == s_paint_detail_sel) {
    return;
  }
  chrome.clear_body();

  if (mode == 1U) {
    const size_t count = rf_sightings_count();
    if (count == 0) {
      chrome.paint_text(4, kListTopY + 8, "No devices yet", kFontLabel, kTextMuted,
                        kBg);
      s_paint_detail_sel = sel;
      s_paint_detail_mode = mode;
      return;
    }
    if (s_nearby_sel >= count) {
      s_nearby_sel = count - 1;
    }
    RfDevice d{};
    if (!rf_sightings_get(s_nearby_sel, &d)) {
      chrome.paint_text(4, kListTopY + 8, "Device gone", kFontLabel, kTextMuted,
                        kBg);
      return;
    }
    const char *vendor = rf_vendor(d);
    const DevIcon icon = classify_rf_device(d, vendor);
    char buf[64];
    char val[24];
    int y = kListTopY;
    draw_dev_icon(tft, icon, 4, y, kAccent, kBg);
    if (vendor) {
      snprintf(buf, sizeof(buf), "%s · %s", vendor, dev_icon_label(icon));
    } else {
      snprintf(buf, sizeof(buf), "%s · %s", d.mac, dev_icon_label(icon));
    }
    chrome.paint_text(22, y + 2, buf, kFontLabel, kText, kBg);
    y += 20;
    chrome.paint_divider(y);
    y += 4;
    chrome.paint_section_label(y, "IDENTITY");
    y += kSectionH + 2;
    chrome.paint_field(y, "MAC", d.mac);
    y += kFieldH;
    chrome.paint_section_label(y, "RF");
    y += kSectionH + 2;
    snprintf(val, sizeof(val), "%d dBm", d.rssi);
    chrome.paint_field(y, "RSSI", val);
    y += kFieldH;
    if (strcmp(d.kind, "wifi") == 0 && d.channel > 0) {
      snprintf(val, sizeof(val), "%u", (unsigned)d.channel);
      chrome.paint_field(y, "Channel", val);
      y += kFieldH;
    }
    if (strcmp(d.kind, "ble") == 0 && d.label[0] &&
        strcmp(d.label, "ble") != 0) {
      chrome.paint_field(y, "Name", d.label);
      y += kFieldH;
    }
    if (d.has_mfgrid) {
      snprintf(val, sizeof(val), "%u", (unsigned)d.mfgrid);
      chrome.paint_field(y, "Mfgrid", val);
      y += kFieldH;
    }
    snprintf(val, sizeof(val), "%lu", (unsigned long)d.seen);
    chrome.paint_field(y, "Seen", val);
    y += kFieldH;
    paint_location_section(y, d.has_gps, d.lat, d.lon, d.has_utc, d.utc_year,
                         d.utc_month, d.utc_day, d.utc_hour, d.utc_minute,
                         d.utc_second);
    snprintf(val, sizeof(val), "%u / %u", (unsigned)(s_nearby_sel + 1),
             (unsigned)count);
    chrome.paint_field(y, "Index", val);
    s_paint_detail_sel = s_nearby_sel;
    s_paint_detail_mode = mode;
    return;
  }

  if (s_hit_count == 0) {
    chrome.paint_text(4, kListTopY + 8, "No hits yet", kFontLabel, kTextMuted, kBg);
    s_paint_detail_sel = s_list_sel;
    return;
  }
  if (s_list_sel >= s_hit_count) {
    s_list_sel = s_hit_count - 1;
  }

  const HitLine &h = s_hits[s_list_sel];
  const uint8_t conf =
      flock_det_confidence(h.kind, h.method, h.name, h.has_name);
  const char *vendor = h.has_mac ? oui_vendor_name(h.mac_raw) : nullptr;
  char val[24];
  char buf[64];
  int y = kListTopY;
  draw_dev_icon(tft, DevIcon::kCamera, 4, y, kFlock, kBg);
  if (vendor) {
    snprintf(buf, sizeof(buf), "Flock · %s", vendor);
  } else {
    snprintf(buf, sizeof(buf), "Flock camera");
  }
  chrome.paint_text(22, y + 2, buf, kFontLabel, kText, kBg);
  if (flock_method_is_probe(h.method)) {
    chrome.paint_badge(disp().width() - 50, y + 1, "PROBE", kBg, kFlock);
  }
  y += 20;
  chrome.paint_divider(y);
  y += 4;
  chrome.paint_section_label(y, "DETECTION");
  y += kSectionH + 2;
  chrome.paint_text(4, y, "Confidence", kFontLabel, kTextMuted, kBg);
  disp().setTextColor(confidence_color(conf), kBg);
  disp().setTextDatum(TR_DATUM);
  disp().drawString(flock_confidence_label(conf), disp().width() - 4, y, kFontLabel);
  disp().setTextDatum(TL_DATUM);
  y += kFieldH;
  chrome.paint_field(y, "Method", flock_method_short(h.method));
  y += kFieldH;
  paint_match_why(y, hit_line_kind(h), h.method, h.mac_raw, h.has_mac, h.name,
                  h.has_name, h.mfgrid, h.has_mfgrid);
  chrome.paint_field(y, "MAC", h.mac);
  y += kFieldH;
  chrome.paint_section_label(y, "RF");
  y += kSectionH + 2;
  snprintf(val, sizeof(val), "%d dBm", h.rssi);
  chrome.paint_field(y, "RSSI", val);
  y += kFieldH;
  if (h.has_channel) {
    snprintf(val, sizeof(val), "%u", (unsigned)h.channel);
    chrome.paint_field(y, "Channel", val);
    y += kFieldH;
  }
  if (h.has_name && h.name[0]) {
    chrome.paint_field(y, "Name", h.name);
    y += kFieldH;
  }
  if (h.has_mfgrid) {
    snprintf(val, sizeof(val), "%u", (unsigned)h.mfgrid);
    chrome.paint_field(y, "Mfgrid", val);
    y += kFieldH;
  }
  paint_location_section(y, h.has_gps, h.lat, h.lon, h.has_utc, h.utc_year,
                         h.utc_month, h.utc_day, h.utc_hour, h.utc_minute,
                         h.utc_second);
  snprintf(val, sizeof(val), "%u / %u", (unsigned)(s_list_sel + 1),
           (unsigned)s_hit_count);
  chrome.paint_field(y, "Index", val);
  s_paint_detail_sel = s_list_sel;
  s_paint_detail_mode = mode;
}


void paint_help(bool force) {
  if (s_help_painted && !force) return;
  chrome.clear_body();
  const char *lines[] = {
      "NAVIGATION",
      "  L/R     prev / next page",
      "  U/D     scroll (lists)",
      "  click   select / detail",
      "  space   next page",
      "KEYS",
      "  s       status",
      "  l n     hits / nearby",
      "  d       detail",
      "  h Esc   help",
      "  j k     scroll list",
      "  g G     last / first row",
      "  p       screenshot",
      "  idle    backlight off (key/wake)",
      nullptr,
  };
  int y = kListTopY;
  for (int i = 0; lines[i]; i++) {
    const bool section = lines[i][0] != ' ';
    chrome.paint_text(4, y, lines[i], kFontLabel,
                      section ? kAccent : kText, kBg);
    y += section ? 14 : 12;
  }
  chrome.paint_divider(y + 2);
  y += 8;
  chrome.paint_text(4, y + 2, "trackball click = select", kFontLabel, kTextMuted,
                    kBg);
  s_help_painted = true;
}

using ScreenPainter = void (*)(bool force);

void paint_status_wrapper(bool force) { paint_status(force); }
void paint_list_wrapper(bool force) { paint_list(force); }
void paint_detail_wrapper(bool force) { paint_detail(force); }
void paint_help_wrapper(bool force) { paint_help(force); }
void paint_nearby_wrapper(bool force) { paint_nearby(force); }

ScreenPainter screen_painter(Screen s) {
  switch (s) {
  case Screen::kList:
    return paint_list_wrapper;
  case Screen::kDetail:
    return paint_detail_wrapper;
  case Screen::kHelp:
    return paint_help_wrapper;
  case Screen::kNearby:
    return paint_nearby_wrapper;
  default:
    return paint_status_wrapper;
  }
}

void reset_screen_cache(Screen s) {
  switch (s) {
  case Screen::kList:
    s_paint_list_sel = SIZE_MAX;
    s_paint_list_start = SIZE_MAX;
    s_paint_list_count = SIZE_MAX;
    break;
  case Screen::kDetail:
    s_paint_detail_sel = SIZE_MAX;
    s_paint_detail_mode = 255;
    break;
  case Screen::kHelp:
    s_help_painted = false;
    break;
  case Screen::kNearby:
    s_paint_nearby_sel = SIZE_MAX;
    s_paint_nearby_start = SIZE_MAX;
    s_paint_nearby_count = SIZE_MAX;
    break;
  default:
    s_status_static = false;
    break;
  }
}

void scroll_list(int delta) {
  trackball_note_vertical();
  size_t *sel = nullptr;
  size_t count = 0;
  if (s_screen == Screen::kList) {
    sel = &s_list_sel;
    count = s_hit_count;
  } else if (s_screen == Screen::kNearby ||
             (s_screen == Screen::kDetail &&
              s_detail_source == DetailSource::kNearby)) {
    sel = &s_nearby_sel;
    count = rf_sightings_count();
  } else if (s_screen == Screen::kDetail &&
             s_detail_source == DetailSource::kFlock) {
    sel = &s_list_sel;
    count = s_hit_count;
  } else {
    return;
  }
  if (count == 0 || !sel) return;
  if (delta < 0) {
    if (*sel > 0) (*sel)--;
    else return;
  } else {
    if (*sel + 1 < count) (*sel)++;
    else return;
  }
  s_dirty = true;
  if (s_screen == Screen::kDetail) {
    s_paint_detail_sel = SIZE_MAX;
  }
}

void goto_screen(Screen sc) {
  if (sc == s_screen) return;
  s_screen = sc;
  s_dirty = true;
}

void show_help() {
  if (s_screen == Screen::kHelp) return;
  s_return_screen = s_screen;
  s_screen = Screen::kHelp;
  s_help_painted = false;
  s_help_key_block_until = millis() + 250;
  kb_drain();
  s_dirty = true;
}

void leave_help() {
  if (s_screen != Screen::kHelp) return;
  s_screen = s_return_screen;
  s_help_painted = false;
  s_help_key_block_until = millis() + 150;
  kb_drain();
  s_dirty = true;
}

bool is_carousel(Screen s) {
  return s == Screen::kStatus || s == Screen::kList || s == Screen::kNearby;
}

int carousel_index(Screen s) {
  switch (s) {
  case Screen::kStatus:
    return 0;
  case Screen::kList:
    return 1;
  case Screen::kNearby:
    return 2;
  default:
    return -1;
  }
}

Screen carousel_screen(int idx) {
  idx = (idx % (int)kCarouselPages + (int)kCarouselPages) % (int)kCarouselPages;
  switch (idx) {
  case 1:
    return Screen::kList;
  case 2:
    return Screen::kNearby;
  default:
    return Screen::kStatus;
  }
}

void goto_carousel_page(int idx) { goto_screen(carousel_screen(idx)); }

void carousel_next() {
  if (!is_carousel(s_screen)) return;
  goto_carousel_page(carousel_index(s_screen) + 1);
}

void carousel_prev() {
  if (!is_carousel(s_screen)) return;
  goto_carousel_page(carousel_index(s_screen) - 1);
}

void open_detail() {
  if (s_screen == Screen::kNearby) {
    if (rf_sightings_count() == 0) return;
    s_detail_source = DetailSource::kNearby;
    s_detail_return = Screen::kNearby;
    goto_screen(Screen::kDetail);
    return;
  }
  if (s_screen == Screen::kList ||
      (s_screen == Screen::kDetail &&
       s_detail_source == DetailSource::kFlock)) {
    if (s_hit_count == 0) return;
    s_detail_source = DetailSource::kFlock;
    s_detail_return = Screen::kList;
    goto_screen(Screen::kDetail);
  }
}

void list_move(int delta) { scroll_list(delta); }

void cycle_screen() {
  if (is_carousel(s_screen)) {
    carousel_next();
    return;
  }
  goto_screen(Screen::kStatus);
}

void paint_screen_soft_keys() {
  switch (s_screen) {
  case Screen::kStatus:
    chrome.paint_soft_keys("List", 'l', "Help", 'h', "Nearby", 'n');
    break;
  case Screen::kList:
    chrome.paint_soft_keys("Status", 's', "Detail", 'd', "Nearby", 'n');
    break;
  case Screen::kNearby:
    chrome.paint_soft_keys("List", 'l', "Detail", 'd', "Status", 's');
    break;
  case Screen::kDetail:
    chrome.paint_soft_keys("Back", 0, "", 0, "Help", 'h');
    break;
  case Screen::kHelp:
    chrome.paint_soft_keys("", 0, "Close", 'h', "", 0);
    break;
  default:
    break;
  }
}

void redraw() {
  if (!s_ok) return;
  spi_bus_idle();

  const bool screen_changed = (s_screen != s_painted);
  if (screen_changed) {
    chrome.invalidate_header();
    reset_screen_cache(s_screen);
    s_painted = s_screen;
  }

  const int hdr_page = is_carousel(s_screen) ? carousel_index(s_screen) : -1;
  const bool flock_alert = (s_hit_alert_until > millis());
  chrome.paint_header(screen_title(s_screen), hdr_page, s_bat_mv, s_bat_usb,
                      flock_alert, screen_changed);
  screen_painter(s_screen)(screen_changed);
  if (is_carousel(s_screen)) {
    chrome.paint_page_dots(kCarouselPages, (size_t)carousel_index(s_screen));
  }
  paint_screen_soft_keys();

  s_last_draw = millis();
  s_dirty = false;
}

bool kb_poll_key(char *out) {
  if (!s_kb) return false;
  Wire.requestFrom((uint8_t)TDECK_KB_ADDR, (uint8_t)1);
  if (Wire.available() == 0) return false;
  *out = (char)Wire.read();
  return *out != 0;
}

// Deferred horizontal page change: vertical is instant; L/R commits after a
// short settle unless U/D cancels it (diagonal scroll won't flip pages).
int8_t s_pending_page_dir = 0;
uint32_t s_pending_page_at = 0;
uint32_t s_last_vertical_ms = 0;

void trackball_cancel_page_pending() { s_pending_page_dir = 0; }

void trackball_note_vertical() {
  s_last_vertical_ms = millis();
  trackball_cancel_page_pending();
}

void trackball_schedule_page(int dir) {
  if (!is_carousel(s_screen)) {
    return;
  }
  s_pending_page_dir = (int8_t)dir;
  s_pending_page_at = millis() + (uint32_t)FD_TRACKBALL_H_SETTLE_MS;
}

void poll_trackball_page_pending() {
  if (s_pending_page_dir == 0) {
    return;
  }
  const uint32_t now = millis();
  if (now < s_pending_page_at) {
    return;
  }
  if (now - s_last_vertical_ms < (uint32_t)FD_TRACKBALL_H_SETTLE_MS) {
    trackball_cancel_page_pending();
    return;
  }
  const int8_t dir = s_pending_page_dir;
  trackball_cancel_page_pending();
  if (dir > 0) {
    carousel_next();
  } else if (dir < 0) {
    carousel_prev();
  }
}

void poll_trackball() {
  static bool last_dir[5] = {};
  const uint8_t pins[5] = {TDECK_TBOX_RIGHT, TDECK_TBOX_UP, TDECK_TBOX_LEFT,
                           TDECK_TBOX_DOWN, TDECK_TRACKBALL_BTN};
  for (int i = 0; i < 5; i++) {
    bool pressed = !digitalRead(pins[i]);
    if (pressed == last_dir[i]) continue;
    last_dir[i] = pressed;
    ui_note_input();
    if (!pressed) continue;

    if (s_screen == Screen::kHelp) {
      leave_help();
      continue;
    }

    switch (i) {
    case 0:
      if (is_carousel(s_screen)) {
        trackball_schedule_page(+1);
      } else if (s_screen == Screen::kDetail) {
        goto_screen(s_detail_return);
      }
      break;
    case 1:
      if (s_screen == Screen::kStatus) {
        trackball_note_vertical();
        scroll_status(-1);
      } else if (s_screen == Screen::kList || s_screen == Screen::kDetail ||
                 s_screen == Screen::kNearby) {
        list_move(-1);
      }
      break;
    case 2:
      if (is_carousel(s_screen)) {
        trackball_schedule_page(-1);
      } else if (s_screen == Screen::kDetail &&
                 s_detail_source == DetailSource::kFlock) {
        goto_screen(Screen::kStatus);
      } else if (s_screen == Screen::kDetail &&
                 s_detail_source == DetailSource::kNearby) {
        goto_screen(Screen::kNearby);
      }
      break;
    case 3:
      if (s_screen == Screen::kStatus) {
        trackball_note_vertical();
        scroll_status(1);
      } else if (s_screen == Screen::kList || s_screen == Screen::kDetail ||
                 s_screen == Screen::kNearby) {
        list_move(1);
      }
      break;
    case 4:
      if (is_carousel(s_screen)) {
        if (s_screen == Screen::kStatus) {
          goto_screen(Screen::kList);
        } else {
          open_detail();
        }
      } else if (s_screen == Screen::kDetail) {
        goto_screen(s_detail_return);
      }
      break;
    default:
      break;
    }
  }
  poll_trackball_page_pending();
}

void handle_key(char key) {
  ui_note_input();
  if (millis() < s_help_key_block_until) {
    return;
  }
  if (s_screen == Screen::kHelp) {
    if (key == 27 || key == 'h' || key == 'H' || key == 's' || key == 'S') {
      leave_help();
    }
    return;
  }
  if (key == 'h' || key == 'H') {
    show_help();
    return;
  }
  if (key == 's' || key == 'S' || key == 27) {
    goto_screen(Screen::kStatus);
    return;
  }
  if (key == 'l' || key == 'L') {
    goto_screen(Screen::kList);
    return;
  }
  if (key == 'n' || key == 'N') {
    goto_screen(Screen::kNearby);
    return;
  }
  if (key == 'd' || key == 'D' || key == '\n' || key == '\r') {
    if (s_screen == Screen::kList || s_screen == Screen::kNearby ||
        s_screen == Screen::kDetail) {
      open_detail();
    } else {
      goto_screen(Screen::kList);
    }
    return;
  }
  if (key == ' ' || key == 'o' || key == 'O') {
    cycle_screen();
    return;
  }
  if (key == 'j' || key == 'J') {
    if (s_screen == Screen::kStatus) {
      scroll_status(1);
      return;
    }
    scroll_list(1);
    return;
  }
  if (key == 'k' || key == 'K' || key == '\b' || key == 127) {
    if (s_screen == Screen::kStatus) {
      scroll_status(-1);
      return;
    }
    scroll_list(-1);
    return;
  }
  if (key == 'p' || key == 'P') {
    if (!tdeck_screenshot_busy()) {
      if (!tdeck_screenshot_begin()) {
        serial_out_info("screenshot failed");
      }
      // Do not emit info lines after begin — JSON would land between FDSC and pixels.
    }
    return;
  }
  if (key == 'g') {
    if (s_screen == Screen::kNearby ||
        (s_screen == Screen::kDetail &&
         s_detail_source == DetailSource::kNearby)) {
      const size_t n = rf_sightings_count();
      if (n > 0) {
        s_nearby_sel = n - 1;
        s_dirty = true;
      }
    } else if (s_hit_count > 0) {
      s_list_sel = s_hit_count - 1;
      s_dirty = true;
    }
    return;
  }
  if (key == 'G') {
    if (s_screen == Screen::kNearby ||
        (s_screen == Screen::kDetail &&
         s_detail_source == DetailSource::kNearby)) {
      if (rf_sightings_count() > 0) {
        s_nearby_sel = 0;
        s_dirty = true;
      }
    } else if (s_hit_count > 0) {
      s_list_sel = 0;
      s_dirty = true;
    }
    return;
  }
}

void poll_keyboard() {
  char key = 0;
  while (kb_poll_key(&key)) {
    handle_key(key);
  }
}

}  // namespace

#ifdef FD_ENABLE_SD
bool tdeck_mount_sd(bool hard_reset, char *err, size_t err_len,
                    uint32_t *speed_hz, uint8_t *card_type, int *miso_level) {
  if (!s_ok) {
    if (err_len) {
      strncpy(err, "no display", err_len - 1);
      err[err_len - 1] = '\0';
    }
    if (card_type) *card_type = CARD_NONE;
    if (miso_level) *miso_level = -1;
    return false;
  }

  tdeck_spi_release();
  tdeck_spi_idle();
  delay(20);

  if (hard_reset) {
    SPI.end();
    delay(10);
    SPI.begin(TDECK_SPI_SCK, TDECK_SPI_MISO, TDECK_SPI_MOSI);
    delay(10);
  }

  if (miso_level) {
    *miso_level = digitalRead(TDECK_SPI_MISO);
  }

  const uint32_t speed = 800000U;
  if (speed_hz) *speed_hz = speed;

  if (!SD.begin(TDECK_SD_CS, tft.getSPIinstance(), speed)) {
    if (err_len) {
      strncpy(err, "no response", err_len - 1);
      err[err_len - 1] = '\0';
    }
    if (card_type) *card_type = CARD_NONE;
    if (hard_reset) {
      tft.init();
      tft.setRotation(1);
    }
    return false;
  }

  uint8_t t = SD.cardType();
  if (card_type) *card_type = t;
  if (t == CARD_NONE) {
    if (err_len) {
      strncpy(err, "no card", err_len - 1);
      err[err_len - 1] = '\0';
    }
    SD.end();
    if (hard_reset) {
      tft.init();
      tft.setRotation(1);
    }
    return false;
  }

  if (err_len) {
    strncpy(err, "ok", err_len - 1);
    err[err_len - 1] = '\0';
  }
  return true;
}
#endif

void tdeck_ui_begin() {
  pinMode(TDECK_POWERON, OUTPUT);
  digitalWrite(TDECK_POWERON, HIGH);
  delay(150);  // SD / peripheral rail stabilize (LilyGO POWERON pattern)

  pinMode(TDECK_TFT_CS, OUTPUT);
  pinMode(TDECK_RADIO_CS, OUTPUT);
  pinMode(TDECK_SD_CS, OUTPUT);
  spi_bus_idle();

  pinMode(TDECK_SPI_MISO, INPUT_PULLUP);
  SPI.begin(TDECK_SPI_SCK, TDECK_SPI_MISO, TDECK_SPI_MOSI);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(kBg);

  draw_flockdar_logo(tft, tft.width() / 2 - 7, 72, kFlock, kBg);
  chrome.paint_header("FLOCKDAR", -1, 0, false, false, true);
  tft.drawFastHLine(tft.width() / 2 - 40, 88, 80, kAccent);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(kTextMuted, kBg);
  tft.drawString("WARDRIVE STARTING", tft.width() / 2, 100, kFontLabel);
  char ver[16];
  snprintf(ver, sizeof(ver), "v%s", FD_FW_VERSION);
  tft.setTextColor(kAccentDim, kBg);
  tft.drawString(ver, tft.width() / 2, 114, kFontLabel);
  tft.setTextDatum(TL_DATUM);

  // Fade-in with a left-to-right scan beam sweeping across a 6 px band.
  // A bright leading edge (kFlock) trails kAccentDim behind it — like radar.
  static constexpr int kScanY = 130;
  static constexpr int kScanH = 6;
  static constexpr int kBeamW = 24;
  pinMode(TDECK_BL_PIN, OUTPUT);
  for (int i = 0; i <= 16; ++i) {
    tdeck_set_brightness((uint8_t)i);
    const int w = tft.width();
    const int beam_x = i * w / 16;
    if (beam_x > 0)
      tft.fillRect(0, kScanY, beam_x, kScanH, kAccentDim);
    const int bw = min(kBeamW, w - beam_x);
    if (bw > 0)
      tft.fillRect(beam_x, kScanY, bw, kScanH, kFlock);
    const int ahead = beam_x + kBeamW;
    if (ahead < w)
      tft.fillRect(ahead, kScanY, w - ahead, kScanH, kBg);
    delay(30);
  }
  // Leave a dim full-width bar; tdeck_boot_step will replace it with the
  // framed progress bar on the first call from main.cpp.
  tft.fillRect(0, kScanY, tft.width(), kScanH, kAccentDim);

  s_ok = true;

  pinMode(TDECK_TRACKBALL_BTN, INPUT_PULLUP);
  pinMode(TDECK_TBOX_RIGHT, INPUT_PULLUP);
  pinMode(TDECK_TBOX_UP, INPUT_PULLUP);
  pinMode(TDECK_TBOX_LEFT, INPUT_PULLUP);
  pinMode(TDECK_TBOX_DOWN, INPUT_PULLUP);

  Wire.begin(TDECK_I2C_SDA, TDECK_I2C_SCL);
  delay(100);
  Wire.beginTransmission(TDECK_KB_ADDR);
  s_kb = (Wire.endTransmission() == 0);

  pinMode(TDECK_BAT_ADC, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  s_last_bat_poll = 0;
  poll_battery();

  s_last_input_ms = millis();
  s_last_draw = millis();
  s_dirty = true;
}

void tdeck_boot_step(const char *label, uint8_t pct) {
  static constexpr int kBarX = 80;
  static constexpr int kBarY = 130;
  static constexpr int kBarW = 160;
  static constexpr int kBarH = 6;
  static constexpr int kLabelY = 146;

  // Clear bar + label region
  tft.fillRect(0, kBarY - 1, tft.width(), kBarH + 2 + 14, kBg);

  // Bar outline
  tft.drawRect(kBarX - 1, kBarY - 1, kBarW + 2, kBarH + 2, kAccentDim);

  // Fill
  const int fill = (int)pct * kBarW / 100;
  if (fill > 0)
    tft.fillRect(kBarX, kBarY, fill, kBarH, pct >= 100 ? kFlock : kAccent);

  // Label
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(pct >= 100 ? kFlock : kTextMuted, kBg);
  tft.drawString(label, tft.width() / 2, kLabelY, kFontLabel);
  tft.setTextDatum(TL_DATUM);
}

void tdeck_ui_note(const Detection &d) {
  if (d.kind == DET_GPS) {
    note_gps(d);
    return;
  }
  push_hit(d);
  ui_note_input();
}

void tdeck_ui_loop() {
  if (!s_ok) return;
  poll_battery();
  poll_trackball();
  poll_keyboard();
  poll_trackball_page_pending();
  poll_display_sleep();

  if (s_display_asleep) {
    return;
  }

  if (s_screen == Screen::kStatus &&
      wifi_scanner_channel() != s_paint_ch) {
    s_dirty = true;
  }

#ifdef FD_ENABLE_GPS
  if (s_screen == Screen::kStatus) {
    bool gf = false;
    uint32_t nc = 0;
    uint8_t gs = 0;
    gps_status(&gf, nullptr, nullptr, &nc, &gs);
    const bool nmea_ok = nc >= 10;
    if (gf != s_paint_gps_fix || nmea_ok != s_paint_gps_nmea ||
        gs != s_paint_sats) {
      s_dirty = true;
    }
  }
#endif

#ifdef FD_ENABLE_SD
  if (s_screen == Screen::kStatus && sdlog_ok() != s_paint_sd) {
    s_dirty = true;
  }
#endif

  if (s_screen == Screen::kNearby &&
      rf_sightings_count() != s_paint_nearby_count) {
    s_dirty = true;
  }

  if (s_hit_alert_until != 0 && millis() >= s_hit_alert_until) {
    s_hit_alert_until = 0;
    chrome.invalidate_header();
    s_dirty = true;
  }

  if (s_dirty) {
    redraw();
  }
}

void tdeck_spi_release() {
  if (!s_ok) return;
  tft.endWrite();
  tdeck_spi_idle();
}

static bool s_screenshot_busy = false;

struct ScreenshotState {
  int w = 0;
  int h = 0;
  int pitch = 0;  // sprite _iwidth (pixels per RAM row; may exceed w)
  int y = 0;
  const uint16_t *pixels = nullptr;
  bool active = false;
};

static ScreenshotState s_scr;
static bool s_scr_paused = false;

static void screenshot_finish(bool ok) {
  if (s_scr_paused) {
    wifi_scanner_resume();
    ble_scanner_resume();
#ifdef FD_ENABLE_SD
    sdlog_bus_hold(false);
#endif
    s_scr_paused = false;
  }
  if (s_scr.active) {
    serial_out_info(ok ? "screenshot done" : "screenshot aborted");
  }
  s_scr = {};
  s_screenshot_busy = false;
}

bool tdeck_screenshot_busy() { return s_screenshot_busy; }

bool tdeck_screenshot_begin() {
  if (!s_ok || s_screenshot_busy) {
    return false;
  }
#ifdef FD_ENABLE_SD
  if (sdlog_host_busy()) {
    return false;
  }
#endif

  wifi_scanner_suspend();
  ble_scanner_suspend();
#ifdef FD_ENABLE_SD
  sdlog_bus_hold(true);
#endif
  s_scr_paused = true;

  tdeck_spi_release();
  tdeck_spi_idle();

  if (!s_snap.created()) {
    s_snap.setColorDepth(16);
    if (!s_snap.createSprite(tft.width(), tft.height())) {
      screenshot_finish(false);
      serial_out_info("screenshot fail: sprite alloc");
      return false;
    }
  }
  s_snap.setRotation(tft.getRotation());
  s_snap.fillScreen(kBg);

  s_status_static = false;
  invalidate_status_paint_cache();

  ui_set_draw_target(s_snap);
  redraw();
  ui_set_draw_target(tft);

  const int w = s_snap.width();
  const int h = s_snap.height();
  const int pitch = s_snap.rowPitch();
  if (w <= 0 || h <= 0 || pitch < w) {
    screenshot_finish(false);
    serial_out_info("screenshot fail: size");
    return false;
  }

  s_scr.pixels = static_cast<const uint16_t *>(s_snap.getPointer());
  if (!s_scr.pixels) {
    screenshot_finish(false);
    serial_out_info("screenshot fail: buffer");
    return false;
  }

  const size_t row_bytes = (size_t)w * 2U;
  const size_t total = row_bytes * (size_t)h;
  char hdr[240];
  snprintf(hdr, sizeof(hdr),
           "{\"v\":%d,\"type\":\"screenshot\",\"fw\":\"%s\",\"w\":%d,\"h\":%d,"
           "\"pitch\":%d,\"bpp\":16,\"len\":%lu,\"ts_ms\":%lu,\"src\":\"sprite\"}",
           FD_PROTO_VERSION, FD_FW_VERSION, w, h, pitch, (unsigned long)total,
           (unsigned long)millis());
  serial_out_raw(hdr);
  static const uint8_t kShotMagic[] = {'F', 'D', 'S', 'C'};
  serial_out_usb_write(kShotMagic, sizeof(kShotMagic));

  s_scr.w = w;
  s_scr.h = h;
  s_scr.pitch = pitch;
  s_scr.y = 0;
  s_scr.active = true;
  s_screenshot_busy = true;
  return true;
}

void tdeck_screenshot_poll() {
  if (!s_scr.active) {
    return;
  }

  const int w = s_scr.w;
  const int h = s_scr.h;
  const int pitch = s_scr.pitch > 0 ? s_scr.pitch : w;
  const size_t row_bytes = (size_t)w * 2U;

  constexpr int kRowsPerTick = 8;
  for (int n = 0; n < kRowsPerTick && s_scr.y < h; n++) {
    const uint16_t *row =
        s_scr.pixels + (size_t)s_scr.y * (size_t)pitch;
    serial_out_usb_write(reinterpret_cast<const uint8_t *>(row), row_bytes);
    s_scr.y++;
    esp_task_wdt_reset();
  }

  if (s_scr.y >= h) {
    screenshot_finish(true);
  }
}

#endif  // FD_ENABLE_TDECK_UI
