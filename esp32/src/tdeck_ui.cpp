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
constexpr uint32_t kRedrawMs = 200;

enum class Screen : uint8_t { kStatus = 0, kList = 1 };

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
uint8_t s_brightness = 12;

// LilyGO T-Deck backlight (16 levels, from factory UnitTest sketch).
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

void spi_bus_idle() {
  digitalWrite(TDECK_TFT_CS, HIGH);
  digitalWrite(TDECK_RADIO_CS, HIGH);
  digitalWrite(FD_SD_CS, HIGH);
}

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
  s_dirty = true;
}

void note_gps(const Detection &d) {
  s_lat = d.lat;
  s_lon = d.lon;
  s_gps_fix = true;
  s_dirty = true;
}

void draw_header(const char *title) {
  tft.fillRect(0, 0, tft.width(), 22, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(title, 4, 4, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
}

void draw_status() {
  draw_header("flockdar");
  int y = 28;
  tft.setTextSize(1);
  tft.drawString("Status", 4, y, 2);
  y += 20;

  char buf[64];
  snprintf(buf, sizeof(buf), "Hits: %lu", (unsigned long)s_hit_total);
  tft.drawString(buf, 4, y);
  y += 14;

  snprintf(buf, sizeof(buf), "WiFi ch: %u", wifi_scanner_channel());
  tft.drawString(buf, 4, y);
  y += 14;

  if (s_gps_fix) {
    snprintf(buf, sizeof(buf), "GPS: %.5f", s_lat);
    tft.drawString(buf, 4, y);
    y += 14;
    snprintf(buf, sizeof(buf), "     %.5f", s_lon);
    tft.drawString(buf, 4, y);
  } else {
    tft.drawString("GPS: no fix", 4, y);
  }
  y += 18;

#ifdef FD_ENABLE_SD
  if (sdlog_ok()) {
    snprintf(buf, sizeof(buf), "SD: %s", sdlog_path());
    tft.drawString(buf, 4, y);
  } else {
    tft.drawString("SD: not mounted", 4, y);
  }
  y += 14;
#endif

  if (s_hit_count > 0) {
    const HitLine &h = s_hits[s_hit_count - 1];
    tft.drawString("Last hit:", 4, y, 2);
    y += 18;
    tft.drawString(h.mac, 4, y);
    y += 14;
    snprintf(buf, sizeof(buf), "%s %s rssi %d", h.kind, h.method, h.rssi);
    tft.drawString(buf, 4, y);
  } else {
    tft.drawString("Scanning...", 4, y + 8);
  }

  y = tft.height() - 28;
  tft.setTextColor(TFT_DARKGREY);
  tft.drawString("Ball: L/R screen  U/D scroll", 4, y);
  tft.drawString("Click: list  +/- bright", 4, y + 12);
  tft.setTextColor(TFT_WHITE);
}

void draw_list() {
  draw_header("Hits");
  const int row_h = 36;
  const int visible = (tft.height() - 30) / row_h;
  if (s_hit_count == 0) {
    tft.drawString("No detections yet", 4, 32);
    return;
  }
  if (s_list_sel >= s_hit_count) s_list_sel = s_hit_count - 1;

  size_t start = 0;
  if (s_hit_count > (size_t)visible) {
    if (s_list_sel >= (size_t)visible) {
      start = s_list_sel - (size_t)visible + 1;
    }
  }

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
}

void redraw() {
  if (!s_ok) return;
  spi_bus_idle();
  tft.fillScreen(TFT_BLACK);
  if (s_screen == Screen::kStatus) {
    draw_status();
  } else {
    draw_list();
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
  pinMode(TDECK_POWERON, OUTPUT);
  digitalWrite(TDECK_POWERON, HIGH);

  pinMode(TDECK_TFT_CS, OUTPUT);
  pinMode(TDECK_RADIO_CS, OUTPUT);
  pinMode(FD_SD_CS, OUTPUT);
  spi_bus_idle();

  pinMode(TDECK_BL_PIN, OUTPUT);
  pinMode(TDECK_TRACKBALL_BTN, INPUT_PULLUP);
  pinMode(TDECK_TBOX_RIGHT, INPUT_PULLUP);
  pinMode(TDECK_TBOX_UP, INPUT_PULLUP);
  pinMode(TDECK_TBOX_LEFT, INPUT_PULLUP);
  pinMode(TDECK_TBOX_DOWN, INPUT_PULLUP);

  pinMode(TDECK_SPI_MISO, INPUT_PULLUP);
  SPI.begin(TDECK_SPI_SCK, TDECK_SPI_MISO, TDECK_SPI_MOSI);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tdeck_set_brightness(s_brightness);
  s_ok = true;

  Wire.begin(TDECK_I2C_SDA, TDECK_I2C_SCL);
  delay(100);
  Wire.beginTransmission(TDECK_KB_ADDR);
  s_kb = (Wire.endTransmission() == 0);

  draw_header("flockdar");
  tft.drawString("Wardrive starting...", 4, 40, 2);
  s_last_draw = millis();
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

  uint32_t now = millis();
  if (s_dirty || (now - s_last_draw >= kRedrawMs)) {
    redraw();
  }
}

#endif  // FD_ENABLE_TDECK_UI
