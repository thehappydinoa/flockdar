#include "tdeck_ui.h"

#ifdef FD_ENABLE_TDECK_UI

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Wire.h>

#include "config.h"
#include "tdeck_board.h"
#include "wifi_scanner.h"

#ifdef FD_ENABLE_SD
#include "sdlog.h"
#endif

namespace {

constexpr size_t kMaxHits = 24;
constexpr uint32_t kHitRedrawMs = 300;  // cap hit-driven UI churn while wardriving

enum class Screen : uint8_t { kStatus = 0, kList = 1 };

// Status screen layout (fixed positions — update fields in place, no full clear).
constexpr int kHdrH = 22;
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
  int rssi;
  uint8_t channel;
};

TFT_eSPI tft;
bool s_ok = false;
bool s_kb = false;

uint32_t s_hit_total = 0;
HitLine s_hits[kMaxHits];
size_t s_hit_count = 0;
size_t s_list_sel = 0;
Screen s_screen = Screen::kStatus;

double s_lat = 0;
double s_lon = 0;
bool s_gps_fix = false;

uint32_t s_last_draw = 0;
bool s_dirty = true;
uint8_t s_brightness = 16;
Screen s_painted = Screen::kStatus;
bool s_status_static = false;

// Last values painted on the status screen (for partial updates).
uint32_t s_paint_hits = UINT32_MAX;
uint8_t s_paint_ch = 255;
bool s_paint_gps = false;
double s_paint_lat = 0;
double s_paint_lon = 0;
bool s_paint_sd = false;
char s_paint_sd_path[24] = "";
size_t s_paint_hit_count = SIZE_MAX;
char s_paint_last_mac[18] = "";
char s_paint_last_detail[48] = "";
size_t s_paint_list_sel = SIZE_MAX;
size_t s_paint_list_start = SIZE_MAX;
size_t s_paint_list_count = SIZE_MAX;

// LilyGO T-Deck backlight
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
  if (now - s_last_draw >= kHitRedrawMs) {
    s_dirty = true;
  }
}

void note_gps(const Detection &d) {
  s_lat = d.lat;
  s_lon = d.lon;
  if (!s_gps_fix || s_lat != s_paint_lat || s_lon != s_paint_lon) {
    s_gps_fix = true;
    s_dirty = true;
  }
}

void draw_header(const char *title) {
  tft.fillRect(0, 0, tft.width(), kHdrH, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(title, 4, 4, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
}

void paint_line(int y, int h, const char *text, uint8_t font = 1) {
  tft.fillRect(0, y, tft.width(), h, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(text, 4, y, font);
}

void paint_status_static() {
  draw_header("flockdar");
  tft.drawString("Status", 4, kStatY, 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Ball: L/R screen  U/D scroll", 4, kFootY);
  tft.drawString("Click: list  +/- bright", 4, kFootY + 12);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  s_status_static = true;
}

void paint_status_dynamic(bool force) {
  char buf[64];

  if (force || s_hit_total != s_paint_hits) {
    snprintf(buf, sizeof(buf), "Hits: %lu", (unsigned long)s_hit_total);
    paint_line(kHitsY, 14, buf);
    s_paint_hits = s_hit_total;
  }

  uint8_t ch = wifi_scanner_channel();
  if (force || ch != s_paint_ch) {
    snprintf(buf, sizeof(buf), "WiFi ch: %u", ch);
    paint_line(kChY, 14, buf);
    s_paint_ch = ch;
  }

  if (force || s_gps_fix != s_paint_gps || s_lat != s_paint_lat ||
      s_lon != s_paint_lon) {
    if (s_gps_fix) {
      snprintf(buf, sizeof(buf), "GPS: %.5f", s_lat);
      paint_line(kGpsY, 14, buf);
      snprintf(buf, sizeof(buf), "     %.5f", s_lon);
      paint_line(kGpsY + 14, 14, buf);
    } else {
      paint_line(kGpsY, 28, "GPS: no fix");
    }
    s_paint_gps = s_gps_fix;
    s_paint_lat = s_lat;
    s_paint_lon = s_lon;
  }

#ifdef FD_ENABLE_SD
  bool sd = sdlog_ok();
  if (force || sd != s_paint_sd ||
      strcmp(s_paint_sd_path, sd ? sdlog_path() : "") != 0) {
    if (sd) {
      snprintf(buf, sizeof(buf), "SD: %s", sdlog_path());
      paint_line(kSdY, 14, buf);
      strncpy(s_paint_sd_path, sdlog_path(), sizeof(s_paint_sd_path) - 1);
    } else {
      paint_line(kSdY, 14, "SD: not mounted");
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
      tft.drawString("Last hit:", 4, kLastY, 2);
      tft.drawString(h.mac, 4, kLastY + 18);
      snprintf(buf, sizeof(buf), "%s %s rssi %d", h.kind, h.method, h.rssi);
      tft.drawString(buf, 4, kLastY + 32);
      strncpy(s_paint_last_mac, h.mac, sizeof(s_paint_last_mac) - 1);
      strncpy(s_paint_last_detail, buf, sizeof(s_paint_last_detail) - 1);
    } else {
      tft.drawString("Scanning...", 4, kLastY + 8);
      s_paint_last_mac[0] = '\0';
      s_paint_last_detail[0] = '\0';
    }
    s_paint_hit_count = s_hit_count;
  }
}

void enter_status_screen(bool force_full) {
  if (force_full || s_painted != Screen::kStatus) {
    tft.fillRect(0, kHdrH, tft.width(), tft.height() - kHdrH, TFT_BLACK);
    s_status_static = false;
    s_paint_hits = UINT32_MAX;
    s_paint_ch = 255;
    s_paint_gps = !s_gps_fix;
    s_paint_hit_count = SIZE_MAX;
    s_painted = Screen::kStatus;
  }
  if (!s_status_static) {
    paint_status_static();
  }
  paint_status_dynamic(force_full || !s_status_static);
}

void paint_list(bool force) {
  draw_header("Hits");
  const int row_h = 36;
  const int visible = (tft.height() - 30) / row_h;
  if (s_hit_count == 0) {
    if (force) {
      tft.fillRect(0, kHdrH, tft.width(), tft.height() - kHdrH, TFT_BLACK);
      draw_header("Hits");
      tft.drawString("No detections yet", 4, 32);
    }
    s_paint_list_sel = s_list_sel;
    s_paint_list_start = 0;
    return;
  }
  if (s_list_sel >= s_hit_count) {
    s_list_sel = s_hit_count - 1;
  }

  size_t start = 0;
  if (s_hit_count > (size_t)visible) {
    if (s_list_sel >= (size_t)visible) {
      start = s_list_sel - (size_t)visible + 1;
    }
  }

  if (force || start != s_paint_list_start || s_list_sel != s_paint_list_sel ||
      s_hit_count != s_paint_list_count) {
    tft.fillRect(0, kHdrH, tft.width(), tft.height() - kHdrH, TFT_BLACK);
    draw_header("Hits");
    int y = 26;
    for (size_t i = start; i < s_hit_count && (int)(i - start) < visible; i++) {
      const HitLine &h = s_hits[i];
      const bool sel = (i == s_list_sel);
      uint16_t bg = sel ? TFT_NAVY : TFT_BLACK;
      tft.fillRect(0, y, tft.width(), row_h - 2, bg);
      tft.setTextColor(TFT_WHITE, bg);
      tft.drawString(h.mac, 4, y + 2, 2);
      char detail[48];
      snprintf(detail, sizeof(detail), "%s %s %d dBm", h.kind, h.method, h.rssi);
      tft.drawString(detail, 4, y + 18);
      y += row_h;
    }
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    char footer[32];
    snprintf(footer, sizeof(footer), "%u/%u", (unsigned)(s_list_sel + 1),
             (unsigned)s_hit_count);
    tft.drawString(footer, tft.width() - 40, tft.height() - 16);
    s_paint_list_sel = s_list_sel;
    s_paint_list_start = start;
    s_paint_list_count = s_hit_count;
  }
}

void redraw() {
  if (!s_ok) return;
  spi_bus_idle();
  const bool screen_changed = (s_screen != s_painted);
  if (s_screen == Screen::kStatus) {
    enter_status_screen(screen_changed);
  } else {
    if (screen_changed) {
      s_paint_list_sel = SIZE_MAX;
      s_paint_list_start = SIZE_MAX;
      s_paint_list_count = SIZE_MAX;
      s_painted = Screen::kList;
    }
    paint_list(screen_changed);
  }
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
    bool pressed = !digitalRead(pins[i]);  // active low with pull-up
    if (pressed == last_dir[i]) continue;
    last_dir[i] = pressed;
    if (!pressed) continue;

    switch (i) {
    case 0:  // right -> list screen
      s_screen = Screen::kList;
      s_dirty = true;
      break;
    case 1:  // up
      if (s_screen == Screen::kList && s_list_sel > 0) {
        s_list_sel--;
        s_dirty = true;
      }
      break;
    case 2:  // left -> status
      s_screen = Screen::kStatus;
      s_dirty = true;
      break;
    case 3:  // down
      if (s_screen == Screen::kList && s_list_sel + 1 < s_hit_count) {
        s_list_sel++;
        s_dirty = true;
      }
      break;
    case 4:  // click -> toggle list / status
      s_screen = (s_screen == Screen::kStatus) ? Screen::kList : Screen::kStatus;
      s_dirty = true;
      break;
    default:
      break;
    }
  }
}

void poll_keyboard() {
  char key = 0;
  if (!kb_poll_key(&key)) return;

  if (key == '+' || key == '=') {
    if (s_brightness < 16) s_brightness++;
    tdeck_set_brightness(s_brightness);
  } else if (key == '-' || key == '_') {
    if (s_brightness > 1) s_brightness--;
    tdeck_set_brightness(s_brightness);
  } else if (key == 'l' || key == 'L') {
    s_screen = Screen::kList;
    s_dirty = true;
  } else if (key == 's' || key == 'S' || key == 27) {  // ESC
    s_screen = Screen::kStatus;
    s_dirty = true;
  } else if (key == '\b' || key == 127) {
    if (s_screen == Screen::kList && s_list_sel > 0) {
      s_list_sel--;
      s_dirty = true;
    }
  } else if (key == '\n' || key == '\r' || key == ' ') {
    s_screen = (s_screen == Screen::kStatus) ? Screen::kList : Screen::kStatus;
    s_dirty = true;
  }
}

}  // namespace

void tdeck_ui_begin() {
  // LilyGO HelloWorld / UnitTest bring-up order.
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

  draw_header("flockdar");
  tft.drawString("Wardrive starting...", 4, 40, 2);

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
  poll_trackball();
  poll_keyboard();

  if (s_screen == Screen::kStatus &&
      wifi_scanner_channel() != s_paint_ch) {
    s_dirty = true;
  }

#ifdef FD_ENABLE_SD
  if (sdlog_ok() != s_paint_sd) {
    s_dirty = true;
  }
#endif

  if (s_dirty) {
    redraw();
  }
}

#endif  // FD_ENABLE_TDECK_UI
