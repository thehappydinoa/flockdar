#include "tdeck_ui_draw.h"

#ifdef FD_ENABLE_TDECK_UI

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include <TFT_eSPI.h>

namespace {

constexpr int kBatW = 118;

}  // namespace

TdeckChrome::TdeckChrome(TFT_eSPI &tft)
    : tft_(tft), title_{0}, bat_mv_(0), bat_usb_(false), header_ok_(false) {}

uint8_t TdeckChrome::battery_percent(uint16_t mv) {
  if (mv >= 4200) return 100;
  if (mv <= 3300) return 0;
  return (uint8_t)((mv - 3300) * 100 / 900);
}

void TdeckChrome::invalidate_header() {
  header_ok_ = false;
  title_[0] = '\0';
  bat_mv_ = 0;
  bat_usb_ = false;
}

void TdeckChrome::paint_title(const char *title) {
  const int bat_w = kBatW;
  const int title_w = tft_.width() - bat_w;
  tft_.fillRect(0, 0, title_w, kHdrH, TFT_DARKGREY);
  tft_.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft_.setTextDatum(TL_DATUM);
  tft_.drawString(title, 4, 4, 2);
  tft_.setTextColor(TFT_WHITE, TFT_BLACK);
  tft_.setTextDatum(TL_DATUM);
  strncpy(title_, title, sizeof(title_) - 1);
  title_[sizeof(title_) - 1] = '\0';
}

void TdeckChrome::paint_battery(uint16_t bat_mv, bool bat_usb) {
  const int x = tft_.width() - kBatW;
  tft_.fillRect(x, 0, kBatW, kHdrH, TFT_DARKGREY);

  char bat[20];
  uint16_t bat_color = TFT_WHITE;
  if (bat_usb) {
    snprintf(bat, sizeof(bat), "USB");
  } else if (bat_mv > 0) {
    snprintf(bat, sizeof(bat), "%u%% %.2fV", (unsigned)battery_percent(bat_mv),
             bat_mv / 1000.0f);
    uint8_t pct = battery_percent(bat_mv);
    if (pct < 10) {
      bat_color = TFT_RED;
    } else if (pct < 25) {
      bat_color = TFT_YELLOW;
    }
  } else {
    strncpy(bat, "bat --", sizeof(bat));
  }

  tft_.setTextColor(bat_color, TFT_DARKGREY);
  tft_.setTextDatum(TR_DATUM);
  tft_.drawString(bat, tft_.width() - 4, 4, 1);
  tft_.setTextColor(TFT_WHITE, TFT_BLACK);
  tft_.setTextDatum(TL_DATUM);

  bat_mv_ = bat_mv;
  bat_usb_ = bat_usb;
}

void TdeckChrome::paint_header(const char *title, uint16_t bat_mv, bool bat_usb,
                               bool force) {
  const bool title_changed =
      force || !header_ok_ || strcmp(title_, title) != 0;
  const bool bat_changed =
      force || !header_ok_ || bat_mv_ != bat_mv || bat_usb_ != bat_usb;

  if (!title_changed && !bat_changed) {
    return;
  }

  if (title_changed) {
    paint_title(title);
  }
  if (bat_changed) {
    paint_battery(bat_mv, bat_usb);
  }
  header_ok_ = true;
}

void TdeckChrome::paint_line(int y, int h, const char *text, uint8_t font) {
  tft_.fillRect(0, y, tft_.width(), h, TFT_BLACK);
  tft_.setTextColor(TFT_WHITE, TFT_BLACK);
  tft_.drawString(text, 4, y, font);
}

void TdeckChrome::paint_text(int x, int y, const char *text, uint8_t font,
                             uint16_t fg, uint16_t bg) {
  tft_.setTextColor(fg, bg);
  tft_.drawString(text, x, y, font);
}

void TdeckChrome::clear_body() {
  tft_.fillRect(0, kHdrH, tft_.width(), tft_.height() - kHdrH, TFT_BLACK);
}

void TdeckChrome::paint_footer(const char *text) {
  tft_.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft_.drawString(text, 4, tft_.height() - 14, 1);
  tft_.setTextColor(TFT_WHITE, TFT_BLACK);
}

void TdeckChrome::paint_scroll_list(size_t count, size_t *sel, size_t *paint_sel,
                                    size_t *paint_start, size_t *paint_count,
                                    ScrollRowFn row_fn, const char *footer,
                                    int top_y, bool force) {
  constexpr int row_h = 36;
  const int visible = (tft_.height() - top_y - 18) / row_h;

  if (count == 0) {
    if (force) {
      clear_body();
      paint_text(4, 32, "Nothing seen yet", 1, TFT_WHITE, TFT_BLACK);
    }
    *paint_sel = *sel;
    *paint_start = 0;
    *paint_count = 0;
    return;
  }

  if (*sel >= count) {
    *sel = count - 1;
  }

  size_t start = 0;
  if (count > (size_t)visible) {
    if (*sel >= (size_t)visible) {
      start = *sel - (size_t)visible + 1;
    }
  }

  auto paint_row = [&](size_t index, size_t row_index, bool selected) {
    char line1[20];
    char line2[48];
    row_fn(index, line1, sizeof(line1), line2, sizeof(line2));
    const int y = top_y + (int)row_index * row_h;
    const uint16_t bg = selected ? TFT_NAVY : TFT_BLACK;
    tft_.fillRect(0, y, tft_.width(), row_h - 2, bg);
    paint_text(4, y + 2, line1, 2, TFT_WHITE, bg);
    paint_text(4, y + 18, line2, 1, TFT_WHITE, bg);
  };

  if (!force && count == *paint_count && start == *paint_start &&
      *sel != *paint_sel && *paint_sel != SIZE_MAX) {
    const size_t old_sel = *paint_sel;
    if (old_sel >= start && old_sel < start + (size_t)visible) {
      paint_row(old_sel, old_sel - start, false);
    }
    if (*sel >= start && *sel < start + (size_t)visible) {
      paint_row(*sel, *sel - start, true);
    }
    *paint_sel = *sel;
    tft_.fillRect(0, tft_.height() - 16, tft_.width(), 16, TFT_BLACK);
    paint_text(4, tft_.height() - 14, footer, 1, TFT_WHITE, TFT_BLACK);
    return;
  }

  if (force || start != *paint_start || *sel != *paint_sel ||
      count != *paint_count) {
    clear_body();
    for (size_t i = start; i < count && (int)(i - start) < visible; i++) {
      paint_row(i, i - start, i == *sel);
    }
    tft_.fillRect(0, tft_.height() - 16, tft_.width(), 16, TFT_BLACK);
    paint_text(4, tft_.height() - 14, footer, 1, TFT_WHITE, TFT_BLACK);
    *paint_sel = *sel;
    *paint_start = start;
    *paint_count = count;
  }
}

#endif  // FD_ENABLE_TDECK_UI
