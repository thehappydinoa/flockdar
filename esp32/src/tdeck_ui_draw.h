// tdeck_ui_draw.h — shared TFT chrome (header, lines, body clear) for T-Deck UI.
#pragma once

#include <stdint.h>

class TFT_eSPI;

class TdeckChrome {
 public:
  static constexpr int kHdrH = 22;

  explicit TdeckChrome(TFT_eSPI &tft);

  // Repaints only title and/or battery regions when their values change.
  void paint_header(const char *title, uint16_t bat_mv, bool bat_usb,
                    bool force = false);

  void paint_line(int y, int h, const char *text, uint8_t font = 1);
  void paint_text(int x, int y, const char *text, uint8_t font,
                  uint16_t fg, uint16_t bg);
  void clear_body();
  void paint_footer(const char *text);
  void invalidate_header();

  TFT_eSPI &tft() { return tft_; }

 private:
  TFT_eSPI &tft_;
  char title_[16];
  uint16_t bat_mv_;
  bool bat_usb_;
  bool header_ok_;

  void paint_title(const char *title);
  void paint_battery(uint16_t bat_mv, bool bat_usb);
  static uint8_t battery_percent(uint16_t mv);
};
