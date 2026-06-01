#include "display.h"

#ifdef FD_ENABLE_TDECK_UI
#include "tdeck_ui.h"

void display_begin() { tdeck_ui_begin(); }
void display_note(const Detection &d) { tdeck_ui_note(d); }
void display_loop() { tdeck_ui_loop(); }

#elif defined(FD_ENABLE_OLED)

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "wifi_scanner.h"

static Adafruit_SSD1306 s_oled(FD_OLED_WIDTH, FD_OLED_HEIGHT, &Wire, -1);
static bool s_ok = false;

static uint32_t s_count = 0;
static char s_last_mac[18] = "--";
static char s_last_kind[8] = "--";
static uint32_t s_last_draw = 0;

void display_begin() {
  Wire.begin(FD_OLED_SDA, FD_OLED_SCL);
  s_ok = s_oled.begin(SSD1306_SWITCHCAPVCC, FD_OLED_ADDR);
  if (!s_ok) return;
  s_oled.clearDisplay();
  s_oled.setTextSize(1);
  s_oled.setTextColor(SSD1306_WHITE);
  s_oled.setCursor(0, 0);
  s_oled.println("flockdar-esp32");
  s_oled.printf("v%s\n", FD_FW_VERSION);
  s_oled.println("scanning...");
  s_oled.display();
}

void display_note(const Detection &d) {
  s_count++;
  if (d.has_mac) {
    snprintf(s_last_mac, sizeof(s_last_mac),
             "%02x:%02x:%02x:%02x:%02x:%02x", d.mac[0], d.mac[1], d.mac[2],
             d.mac[3], d.mac[4], d.mac[5]);
  }
  const char *k = d.kind == DET_WIFI ? "wifi" : d.kind == DET_BLE ? "ble" : "gps";
  strncpy(s_last_kind, k, sizeof(s_last_kind) - 1);
}

void display_loop() {
  if (!s_ok) return;
  uint32_t now = millis();
  if (now - s_last_draw < FD_OLED_REDRAW_MS) return;
  s_last_draw = now;

  s_oled.clearDisplay();
  s_oled.setCursor(0, 0);
  s_oled.println("flockdar-esp32");
  s_oled.printf("hits: %lu\n", (unsigned long)s_count);
  s_oled.printf("ch:   %u\n", wifi_scanner_channel());
  s_oled.printf("last: %s\n", s_last_kind);
  s_oled.println(s_last_mac);
  s_oled.display();
}

#endif  // FD_ENABLE_OLED
