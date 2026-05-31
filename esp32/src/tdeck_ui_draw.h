// tdeck_ui_draw.h — shared TFT chrome (header, lines, body clear) for T-Deck UI.
#pragma once

#include <stddef.h>
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
  // iOS-style page dots for the Status / Hits / Nearby carousel (bottom center).
  void paint_page_dots(size_t count, size_t active);
  void invalidate_header();

  // Scrollable two-line list (shared by Flock hits and nearby RF lists).
  typedef void (*ScrollRowFn)(size_t index, char *line1, size_t line1sz,
                              char *line2, size_t line2sz);
  void paint_scroll_list(size_t count, size_t *sel, size_t *paint_sel,
                         size_t *paint_start, size_t *paint_count,
                         ScrollRowFn row_fn, const char *footer, int top_y,
                         bool force);

  struct IconRow {
    uint8_t icon;  // DevIcon
    char line1[36];
    char line2[44];
  };
  typedef void (*IconRowFn)(size_t index, IconRow *out);
  void paint_icon_scroll_list(size_t count, size_t *sel, size_t *paint_sel,
                              size_t *paint_start, size_t *paint_count,
                              IconRowFn row_fn, const char *footer, int top_y,
                              bool force);

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
