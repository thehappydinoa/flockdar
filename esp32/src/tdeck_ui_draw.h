// tdeck_ui_draw.h — shared TFT chrome for T-Deck UI.
#pragma once

#include <stddef.h>
#include <stdint.h>

class TFT_eSPI;
enum class StatusIcon : uint8_t;

class TdeckChrome {
 public:
  static constexpr int kHdrH = 28;

  explicit TdeckChrome(TFT_eSPI &tft);

  // page_idx: 0=STATUS 1=HITS 2=NEARBY, or -1 for no tab.
  void paint_header(const char *title, int page_idx, uint16_t bat_mv,
                    bool bat_usb, bool flock_alert = false, bool force = false);

  void paint_badge(int x, int y, const char *text, uint16_t fg, uint16_t bg);

  void paint_text(int x, int y, const char *text, uint8_t font, uint16_t fg,
                  uint16_t bg);
  void clear_body();
  void paint_divider(int y);
  void paint_section_label(int y, const char *label);
  void paint_field(int y, const char *label, const char *value);
  void paint_field_icon(int y, StatusIcon icon, const char *label,
                        const char *value);
  void paint_value_right(int y, const char *value, uint8_t font);
  // Word-wrap text; returns y below the last painted line.
  int paint_wrapped_text(int x, int y, int max_w, const char *text,
                         uint8_t font, uint16_t fg, uint16_t bg,
                         int max_lines, int line_h);
  // hotkey: letter to underline in label (0 = none).
  void paint_soft_keys(const char *left, char left_key, const char *center,
                       char center_key, const char *right, char right_key);
  void paint_page_dots(size_t count, size_t active);
  void paint_chrome_bottom();
  void invalidate_header();
  void invalidate_footer();

  void paint_list_row(int y, bool selected, bool flock_accent, const char *line1,
                      uint8_t font1, const char *line2, uint8_t icon = 255,
                      bool has_icon = false);

  typedef void (*ScrollRowFn)(size_t index, char *line1, size_t line1sz,
                              char *line2, size_t line2sz,
                              bool *flock_accent);
  void paint_scroll_list(size_t count, size_t *sel, size_t *paint_sel,
                         size_t *paint_start, size_t *paint_count,
                         ScrollRowFn row_fn, int top_y, bool force);

  struct IconRow {
    uint8_t icon;
    char line1[36];
    char line2[44];
  };
  typedef void (*IconRowFn)(size_t index, IconRow *out);
  void paint_icon_scroll_list(size_t count, size_t *sel, size_t *paint_sel,
                              size_t *paint_start, size_t *paint_count,
                              IconRowFn row_fn, int top_y, bool force,
                              bool flock_accent = false);

  TFT_eSPI &tft() { return *tft_; }
  void bind(TFT_eSPI &target) { tft_ = &target; }

 private:
  TFT_eSPI *tft_;
  char title_[20];
  int page_idx_;
  uint16_t bat_mv_;
  bool bat_usb_;
  bool flock_alert_;
  bool header_ok_;
  char soft_left_[20];
  char soft_center_[20];
  char soft_right_[20];
  char soft_left_key_;
  char soft_center_key_;
  char soft_right_key_;
  bool soft_keys_ok_;
  size_t dots_count_;
  size_t dots_active_;
  bool dots_ok_;

  void paint_title_bar(const char *title, int page_idx, bool flock_alert,
                       uint16_t bar_bg);
  void paint_battery(uint16_t bat_mv, bool bat_usb, bool flock_alert,
                     uint16_t bar_bg);
  void paint_header_background(bool flock_alert);
  void paint_soft_key_label(int x, int y, uint8_t datum, const char *label,
                            char hotkey);
  int text_anchor_x(int anchor_x, uint8_t datum, const char *text,
                    uint8_t font) const;
  void paint_text_datum(int anchor_x, int y, uint8_t datum, const char *text,
                        uint8_t font, uint16_t fg, uint16_t bg);
  static uint8_t battery_percent(uint16_t mv);
  int list_visible_rows(int top_y) const;
  template <typename PaintRow>
  void paint_list_core(size_t count, size_t *sel, size_t *paint_sel,
                       size_t *paint_start, size_t *paint_count,
                       PaintRow row_fn, int top_y, bool force);
};
