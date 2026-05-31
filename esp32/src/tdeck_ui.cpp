#include "tdeck_ui.h"

#ifdef FD_ENABLE_TDECK_UI

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Wire.h>

#include "config.h"
#include "tdeck_board.h"
#include "tdeck_ui_draw.h"
#include "wifi_scanner.h"
#include "ble_scanner.h"
#ifdef FD_ENABLE_GPS
#include "gps.h"
#endif

#ifdef FD_ENABLE_SD
#include "sdlog.h"
#endif

namespace {

constexpr size_t kMaxHits = 24;
constexpr uint32_t kHitRedrawMs = 300;
constexpr uint32_t kBatPollMs = 5000;
constexpr int kHdrH = TdeckChrome::kHdrH;
constexpr int kListRowH = 36;
constexpr int kListTopY = kHdrH + 4;

enum class Screen : uint8_t { kStatus = 0, kList = 1, kDetail = 2, kHelp = 3 };

constexpr int kStatY = 28;
constexpr int kHitsY = 48;
constexpr int kChY = 62;
constexpr int kGpsY = 76;
constexpr int kSdY = 104;
constexpr int kLastY = 132;
constexpr int kFootY = 212;

struct HitLine {
  char mac[18];
  char kind[5];
  char method[16];
  char name[32];
  int rssi;
  uint8_t channel;
  uint16_t mfgrid;
  bool has_name;
  bool has_channel;
  bool has_mfgrid;
};

TFT_eSPI tft;
TdeckChrome chrome(tft);
bool s_ok = false;
bool s_kb = false;

uint32_t s_hit_total = 0;
HitLine s_hits[kMaxHits];
size_t s_hit_count = 0;
size_t s_list_sel = 0;
Screen s_screen = Screen::kStatus;
Screen s_return_screen = Screen::kStatus;

uint32_t s_last_draw = 0;
bool s_dirty = true;
uint8_t s_brightness = 16;
Screen s_painted = Screen::kStatus;
bool s_status_static = false;

uint32_t s_paint_hits = UINT32_MAX;
uint8_t s_paint_ch = 255;
uint32_t s_paint_wifi_rf = 0;
uint32_t s_paint_ble_rf = 0;
bool s_paint_gps_fix = false;
uint32_t s_paint_nmea = 0;
uint8_t s_paint_sats = 255;
bool s_paint_sd = false;
char s_paint_sd_path[24] = "";
size_t s_paint_hit_count = SIZE_MAX;
char s_paint_last_mac[18] = "";
char s_paint_last_detail[48] = "";
size_t s_paint_list_sel = SIZE_MAX;
size_t s_paint_list_start = SIZE_MAX;
size_t s_paint_list_count = SIZE_MAX;
size_t s_paint_detail_sel = SIZE_MAX;

uint16_t s_bat_mv = 0;
bool s_bat_usb = false;
uint32_t s_last_bat_poll = 0;
bool s_help_painted = false;
uint32_t s_help_key_block_until = 0;

bool kb_poll_key(char *out);

void kb_drain() {
  char key = 0;
  while (kb_poll_key(&key)) {
  }
}

const char *screen_title(Screen s) {
  switch (s) {
  case Screen::kList:
    return "Hits";
  case Screen::kDetail:
    return "Detail";
  case Screen::kHelp:
    return "Help";
  default:
    return "flockdar";
  }
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

void spi_bus_idle() { tdeck_spi_idle(); }

void push_hit(const Detection &d) {
  if (d.kind != DET_WIFI && d.kind != DET_BLE) return;
  s_hit_total++;

  HitLine line{};
  if (d.has_mac) {
    snprintf(line.mac, sizeof(line.mac), "%02x:%02x:%02x:%02x:%02x:%02x",
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
  } else {
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

  if (s_hit_count < kMaxHits) {
    s_hits[s_hit_count++] = line;
  } else {
    memmove(&s_hits[0], &s_hits[1], (kMaxHits - 1) * sizeof(HitLine));
    s_hits[kMaxHits - 1] = line;
  }
  if (s_list_sel >= s_hit_count && s_hit_count > 0) {
    s_list_sel = s_hit_count - 1;
  }
  uint32_t now = millis();
  if (now - s_last_draw >= kHitRedrawMs && s_screen != Screen::kHelp) {
    s_dirty = true;
  }
}

void note_gps(const Detection &d) {
  (void)d;
  if (s_screen != Screen::kHelp) {
    s_dirty = true;
  }
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

void paint_status_static() {
  chrome.paint_text(4, kStatY, "Status", 2, TFT_WHITE, TFT_BLACK);
  chrome.paint_footer("h help  l list  j/k scroll");
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("d detail  +/- bright  space", 4, kFootY + 12);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  s_status_static = true;
}

void paint_status_dynamic(bool force) {
  char buf[64];

  if (force || s_hit_total != s_paint_hits) {
    snprintf(buf, sizeof(buf), "Flock hits: %lu", (unsigned long)s_hit_total);
    chrome.paint_line(kHitsY, 14, buf);
    s_paint_hits = s_hit_total;
  }

  uint8_t ch = wifi_scanner_channel();
  uint32_t wifi_rf = wifi_scanner_mgmt_frames();
  uint32_t ble_rf = ble_scanner_adverts();
  if (force || ch != s_paint_ch || wifi_rf != s_paint_wifi_rf ||
      ble_rf != s_paint_ble_rf) {
    snprintf(buf, sizeof(buf), "ch %u  wifi %lu  ble %lu", ch,
             (unsigned long)wifi_rf, (unsigned long)ble_rf);
    chrome.paint_line(kChY, 14, buf);
    s_paint_ch = ch;
    s_paint_wifi_rf = wifi_rf;
    s_paint_ble_rf = ble_rf;
  }

#ifdef FD_ENABLE_GPS
  bool gps_fix = false;
  double gps_lat = 0;
  double gps_lon = 0;
  uint32_t nmea_chars = 0;
  uint8_t gps_sats = 0;
  gps_status(&gps_fix, &gps_lat, &gps_lon, &nmea_chars, &gps_sats);
  if (force || gps_fix != s_paint_gps_fix || nmea_chars != s_paint_nmea ||
      gps_sats != s_paint_sats) {
    if (gps_fix) {
      snprintf(buf, sizeof(buf), "GPS: %.5f", gps_lat);
      chrome.paint_line(kGpsY, 14, buf);
      snprintf(buf, sizeof(buf), "     %.5f", gps_lon);
      chrome.paint_line(kGpsY + 14, 14, buf);
    } else if (nmea_chars < 10) {
      chrome.paint_line(kGpsY, 28, "GPS: no module");
    } else {
      snprintf(buf, sizeof(buf), "GPS: fix.. (%u sats)", (unsigned)gps_sats);
      chrome.paint_line(kGpsY, 14, buf);
    }
    s_paint_gps_fix = gps_fix;
    s_paint_nmea = nmea_chars;
    s_paint_sats = gps_sats;
  }
#endif

#ifdef FD_ENABLE_SD
  bool sd = sdlog_ok();
  if (force || sd != s_paint_sd ||
      strcmp(s_paint_sd_path, sd ? sdlog_path() : "") != 0) {
    if (sd) {
      snprintf(buf, sizeof(buf), "SD: %s", sdlog_path());
      chrome.paint_line(kSdY, 14, buf);
      strncpy(s_paint_sd_path, sdlog_path(), sizeof(s_paint_sd_path) - 1);
    } else {
      chrome.paint_line(kSdY, 14, "SD: not mounted");
      s_paint_sd_path[0] = '\0';
    }
    s_paint_sd = sd;
  }
#endif

  bool last_changed = force || s_hit_count != s_paint_hit_count;
  if (!last_changed && s_hit_count > 0) {
    const HitLine &h = s_hits[s_hit_count - 1];
    char detail[48];
    snprintf(detail, sizeof(detail), "%s %s rssi %d", h.kind, h.method, h.rssi);
    last_changed = (strcmp(s_paint_last_mac, h.mac) != 0 ||
                    strcmp(s_paint_last_detail, detail) != 0);
  }
  if (last_changed) {
    tft.fillRect(0, kLastY, tft.width(), kFootY - kLastY, TFT_BLACK);
    if (s_hit_count > 0) {
      const HitLine &h = s_hits[s_hit_count - 1];
      chrome.paint_text(4, kLastY, "Last hit:", 2, TFT_WHITE, TFT_BLACK);
      chrome.paint_text(4, kLastY + 18, h.mac, 2, TFT_WHITE, TFT_BLACK);
      snprintf(buf, sizeof(buf), "%s %s rssi %d", h.kind, h.method, h.rssi);
      chrome.paint_text(4, kLastY + 32, buf, 1, TFT_WHITE, TFT_BLACK);
      strncpy(s_paint_last_mac, h.mac, sizeof(s_paint_last_mac) - 1);
      strncpy(s_paint_last_detail, buf, sizeof(s_paint_last_detail) - 1);
    } else {
      chrome.paint_text(4, kLastY + 8, "No Flock cameras yet", 1, TFT_WHITE,
                        TFT_BLACK);
      s_paint_last_mac[0] = '\0';
      s_paint_last_detail[0] = '\0';
    }
    s_paint_hit_count = s_hit_count;
  }
}

void paint_status(bool force_full) {
  if (force_full || s_painted != Screen::kStatus) {
    chrome.clear_body();
    s_status_static = false;
    s_paint_hits = UINT32_MAX;
    s_paint_ch = 255;
    s_paint_gps_fix = false;
    s_paint_nmea = 0;
    s_paint_sats = 255;
    s_paint_hit_count = SIZE_MAX;
    s_painted = Screen::kStatus;
  }
  if (!s_status_static) {
    paint_status_static();
  }
  paint_status_dynamic(force_full || !s_status_static);
}

size_t list_window_start(size_t visible) {
  if (s_hit_count <= visible) return 0;
  if (s_list_sel >= visible) return s_list_sel - visible + 1;
  return 0;
}

void paint_list_row(size_t hit_index, size_t row_index, bool selected) {
  const int y = kListTopY + (int)row_index * kListRowH;
  const HitLine &h = s_hits[hit_index];
  const uint16_t bg = selected ? TFT_NAVY : TFT_BLACK;
  tft.fillRect(0, y, tft.width(), kListRowH - 2, bg);
  chrome.paint_text(4, y + 2, h.mac, 2, TFT_WHITE, bg);
  char detail[48];
  snprintf(detail, sizeof(detail), "%s %s %d dBm", h.kind, h.method, h.rssi);
  chrome.paint_text(4, y + 18, detail, 1, TFT_WHITE, bg);
}

void paint_list_footer() {
  char footer[32];
  snprintf(footer, sizeof(footer), "%u/%u  d detail", (unsigned)(s_list_sel + 1),
           (unsigned)s_hit_count);
  tft.fillRect(0, tft.height() - 16, tft.width(), 16, TFT_BLACK);
  chrome.paint_text(4, tft.height() - 14, footer, 1, TFT_WHITE, TFT_BLACK);
}

void paint_list(bool force) {
  const int visible = (tft.height() - kListTopY - 18) / kListRowH;

  if (s_hit_count == 0) {
    if (force) {
      chrome.clear_body();
      chrome.paint_text(4, 32, "No Flock detections yet", 1, TFT_WHITE,
                        TFT_BLACK);
    }
    s_paint_list_sel = s_list_sel;
    s_paint_list_start = 0;
    s_paint_list_count = 0;
    return;
  }

  if (s_list_sel >= s_hit_count) {
    s_list_sel = s_hit_count - 1;
  }

  const size_t start = list_window_start((size_t)visible);

  if (!force && s_hit_count == s_paint_list_count &&
      start == s_paint_list_start && s_list_sel != s_paint_list_sel &&
      s_paint_list_sel != SIZE_MAX) {
    const size_t old_sel = s_paint_list_sel;
    if (old_sel >= start && old_sel < start + (size_t)visible) {
      paint_list_row(old_sel, old_sel - start, false);
    }
    if (s_list_sel >= start && s_list_sel < start + (size_t)visible) {
      paint_list_row(s_list_sel, s_list_sel - start, true);
    }
    s_paint_list_sel = s_list_sel;
    paint_list_footer();
    return;
  }

  if (force || start != s_paint_list_start || s_list_sel != s_paint_list_sel ||
      s_hit_count != s_paint_list_count) {
    chrome.clear_body();
    for (size_t i = start; i < s_hit_count && (int)(i - start) < visible; i++) {
      paint_list_row(i, i - start, i == s_list_sel);
    }
    paint_list_footer();
    s_paint_list_sel = s_list_sel;
    s_paint_list_start = start;
    s_paint_list_count = s_hit_count;
  }
}

void paint_detail(bool force) {
  if (!force && s_painted == Screen::kDetail &&
      s_list_sel == s_paint_detail_sel) {
    return;
  }
  chrome.clear_body();

  if (s_hit_count == 0) {
    chrome.paint_text(4, 32, "No hits yet", 1, TFT_WHITE, TFT_BLACK);
    chrome.paint_footer("s status  l list");
    s_paint_detail_sel = s_list_sel;
    return;
  }
  if (s_list_sel >= s_hit_count) {
    s_list_sel = s_hit_count - 1;
  }

  const HitLine &h = s_hits[s_list_sel];
  char buf[64];
  int y = kListTopY;
  chrome.paint_text(4, y, h.mac, 2, TFT_WHITE, TFT_BLACK);
  y += 20;
  snprintf(buf, sizeof(buf), "%s  %s", h.kind, h.method);
  chrome.paint_text(4, y, buf, 1, TFT_WHITE, TFT_BLACK);
  y += 14;
  snprintf(buf, sizeof(buf), "RSSI: %d dBm", h.rssi);
  chrome.paint_text(4, y, buf, 1, TFT_WHITE, TFT_BLACK);
  y += 14;
  if (h.has_channel) {
    snprintf(buf, sizeof(buf), "Channel: %u", (unsigned)h.channel);
    chrome.paint_text(4, y, buf, 1, TFT_WHITE, TFT_BLACK);
    y += 14;
  }
  if (h.has_name && h.name[0]) {
    snprintf(buf, sizeof(buf), "Name: %s", h.name);
    chrome.paint_text(4, y, buf, 1, TFT_WHITE, TFT_BLACK);
    y += 14;
  }
  if (h.has_mfgrid) {
    snprintf(buf, sizeof(buf), "Mfgrid: %u", (unsigned)h.mfgrid);
    chrome.paint_text(4, y, buf, 1, TFT_WHITE, TFT_BLACK);
    y += 14;
  }
  snprintf(buf, sizeof(buf), "%u / %u", (unsigned)(s_list_sel + 1),
           (unsigned)s_hit_count);
  chrome.paint_text(4, y + 4, buf, 1, TFT_WHITE, TFT_BLACK);
  chrome.paint_footer("s list  j/k hit  h help");
  s_paint_detail_sel = s_list_sel;
}

void paint_help(bool force) {
  if (s_help_painted && !force) return;
  chrome.clear_body();
  const char *lines[] = {
      "s       status / wardrive",
      "l       Flock hit list",
      "d ret   detail for selection",
      "h       this screen",
      "j k     list down / up",
      "g G     last / first hit",
      "space   cycle status/list",
      "+ -     screen brightness",
      "Ball    L/R change screen",
      "        U/D scroll list",
      "        click open detail",
      nullptr,
  };
  int y = kListTopY;
  for (int i = 0; lines[i]; i++) {
    chrome.paint_text(4, y, lines[i], 1, TFT_WHITE, TFT_BLACK);
    y += 12;
  }
  chrome.paint_footer("h or Esc to close");
  s_help_painted = true;
}

using ScreenPainter = void (*)(bool force);

void paint_status_wrapper(bool force) { paint_status(force); }
void paint_list_wrapper(bool force) { paint_list(force); }
void paint_detail_wrapper(bool force) { paint_detail(force); }
void paint_help_wrapper(bool force) { paint_help(force); }

ScreenPainter screen_painter(Screen s) {
  switch (s) {
  case Screen::kList:
    return paint_list_wrapper;
  case Screen::kDetail:
    return paint_detail_wrapper;
  case Screen::kHelp:
    return paint_help_wrapper;
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
    break;
  case Screen::kHelp:
    s_help_painted = false;
    break;
  default:
    s_status_static = false;
    break;
  }
}

void list_move(int delta) {
  if (s_hit_count == 0) return;
  if (delta < 0) {
    if (s_list_sel > 0) s_list_sel--;
    else return;
  } else {
    if (s_list_sel + 1 < s_hit_count) s_list_sel++;
    else return;
  }
  s_dirty = true;
}

void goto_screen(Screen sc) {
  if (sc == Screen::kDetail && s_hit_count == 0) return;
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

void cycle_screen() {
  if (s_screen == Screen::kStatus) {
    goto_screen(Screen::kList);
  } else if (s_screen == Screen::kList) {
    goto_screen(Screen::kStatus);
  } else if (s_screen == Screen::kHelp) {
    leave_help();
  } else {
    goto_screen(Screen::kList);
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

  chrome.paint_header(screen_title(s_screen), s_bat_mv, s_bat_usb, screen_changed);
  screen_painter(s_screen)(screen_changed);

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

void poll_trackball() {
  static bool last_dir[5] = {};
  const uint8_t pins[5] = {TDECK_TBOX_RIGHT, TDECK_TBOX_UP, TDECK_TBOX_LEFT,
                           TDECK_TBOX_DOWN, TDECK_TRACKBALL_BTN};
  for (int i = 0; i < 5; i++) {
    bool pressed = !digitalRead(pins[i]);
    if (pressed == last_dir[i]) continue;
    last_dir[i] = pressed;
    if (!pressed) continue;

    if (s_screen == Screen::kHelp) {
      leave_help();
      continue;
    }

    switch (i) {
    case 0:
      if (s_screen == Screen::kStatus) {
        goto_screen(Screen::kList);
      } else if (s_screen == Screen::kDetail) {
        goto_screen(Screen::kList);
      }
      break;
    case 1:
      if (s_screen == Screen::kList || s_screen == Screen::kDetail) {
        list_move(-1);
      }
      break;
    case 2:
      if (s_screen == Screen::kList || s_screen == Screen::kDetail) {
        goto_screen(Screen::kStatus);
      } else if (s_screen == Screen::kStatus) {
        show_help();
      }
      break;
    case 3:
      if (s_screen == Screen::kList || s_screen == Screen::kDetail) {
        list_move(1);
      }
      break;
    case 4:
      if (s_screen == Screen::kStatus) {
        goto_screen(Screen::kList);
      } else if (s_screen == Screen::kList) {
        goto_screen(Screen::kDetail);
      } else if (s_screen == Screen::kDetail) {
        goto_screen(Screen::kList);
      }
      break;
    default:
      break;
    }
  }
}

void handle_key(char key) {
  if (millis() < s_help_key_block_until) {
    return;
  }

  if (s_screen == Screen::kHelp) {
    if (key == 27 || key == 'h' || key == 'H' || key == 's' || key == 'S') {
      leave_help();
    }
    return;
  }

  if (key == '+' || key == '=') {
    if (s_brightness < 16) s_brightness++;
    tdeck_set_brightness(s_brightness);
    return;
  }
  if (key == '-' || key == '_') {
    if (s_brightness > 1) s_brightness--;
    tdeck_set_brightness(s_brightness);
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
  if (key == 'd' || key == 'D' || key == '\n' || key == '\r') {
    if (s_screen == Screen::kList || s_screen == Screen::kDetail) {
      goto_screen(Screen::kDetail);
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
    if (s_screen == Screen::kStatus && s_hit_count > 0) {
      goto_screen(Screen::kList);
    }
    list_move(1);
    return;
  }
  if (key == 'k' || key == 'K' || key == '\b' || key == 127) {
    if (s_screen == Screen::kStatus && s_hit_count > 0) {
      goto_screen(Screen::kList);
    }
    list_move(-1);
    return;
  }
  if (key == 'g') {
    if (s_hit_count > 0) {
      s_list_sel = s_hit_count - 1;
      s_dirty = true;
    }
    return;
  }
  if (key == 'G') {
    if (s_hit_count > 0) {
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

void tdeck_ui_begin() {
  pinMode(TDECK_POWERON, OUTPUT);
  digitalWrite(TDECK_POWERON, HIGH);

  pinMode(TDECK_TFT_CS, OUTPUT);
  pinMode(TDECK_RADIO_CS, OUTPUT);
  pinMode(TDECK_SD_CS, OUTPUT);
  spi_bus_idle();

  pinMode(TDECK_SPI_MISO, INPUT_PULLUP);
  SPI.begin(TDECK_SPI_SCK, TDECK_SPI_MISO, TDECK_SPI_MOSI);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  chrome.paint_header("flockdar", 0, false, true);
  chrome.paint_text(4, 40, "Wardrive starting...", 2, TFT_WHITE, TFT_BLACK);

  pinMode(TDECK_BL_PIN, OUTPUT);
  for (int i = 0; i <= 16; ++i) {
    tdeck_set_brightness((uint8_t)i);
    delay(30);
  }
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

  s_last_draw = millis();
  s_dirty = true;
}

void tdeck_ui_note(const Detection &d) {
  if (d.kind == DET_GPS) {
    note_gps(d);
    return;
  }
  push_hit(d);
}

void tdeck_ui_loop() {
  if (!s_ok) return;
  poll_battery();
  poll_trackball();
  poll_keyboard();

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
    if (gf != s_paint_gps_fix || nc != s_paint_nmea || gs != s_paint_sats) {
      s_dirty = true;
    }
  }
#endif

#ifdef FD_ENABLE_SD
  if (s_screen == Screen::kStatus && sdlog_ok() != s_paint_sd) {
    s_dirty = true;
  }
#endif

  if (s_dirty) {
    redraw();
  }
}

void tdeck_spi_release() {
  if (!s_ok) return;
  tft.endWrite();
  tdeck_spi_idle();
}

#endif  // FD_ENABLE_TDECK_UI
