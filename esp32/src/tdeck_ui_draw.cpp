#include "tdeck_ui_draw.h"

#ifdef FD_ENABLE_TDECK_UI

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include <TFT_eSPI.h>

#include "tdeck_icons.h"
#include "tdeck_theme.h"

namespace {

constexpr int kBatW = 72;
using namespace TdeckTheme;

const char *page_tab_label(int page_idx) {
  switch (page_idx) {
  case 0:
    return "STATUS";
  case 1:
    return "HITS";
  case 2:
    return "NEARBY";
  default:
    return nullptr;
  }
}

}  // namespace

TdeckChrome::TdeckChrome(TFT_eSPI &tft)
    : tft_(tft),
      title_{0},
      page_idx_(-1),
      bat_mv_(0),
      bat_usb_(false),
      header_ok_(false) {}

uint8_t TdeckChrome::battery_percent(uint16_t mv) {
  if (mv >= 4200) return 100;
  if (mv <= 3300) return 0;
  return (uint8_t)((mv - 3300) * 100 / 900);
}

void TdeckChrome::invalidate_header() {
  header_ok_ = false;
  title_[0] = '\0';
  page_idx_ = -2;
  bat_mv_ = 0;
  bat_usb_ = false;
}

int TdeckChrome::list_visible_rows(int top_y) const {
  return (tft_.height() - top_y - kChromeBottom) / kRowH;
}

void TdeckChrome::paint_title_bar(const char *title, int page_idx) {
  const int title_w = tft_.width() - kBatW;
  tft_.fillRect(0, 0, title_w, kHdrH, kSurface);
  tft_.drawFastHLine(0, kHdrH - 1, tft_.width(), kDivider);

  tft_.setTextColor(kText, kSurface);
  tft_.setTextDatum(TL_DATUM);
  int title_x = 4;
  if (title && strcmp(title, "FLOCKDAR") == 0) {
    draw_flockdar_logo(tft_, 4, 6, kFlock, kSurface);
    title_x = 20;
  }
  tft_.drawString(title, title_x, 6, kFontTitle);

  const char *tab = page_tab_label(page_idx);
  if (tab) {
    tft_.setTextColor(kAccent, kSurface);
    tft_.setTextDatum(TC_DATUM);
    tft_.drawString(tab, tft_.width() / 2, 8, kFontLabel);
  }

  strncpy(title_, title, sizeof(title_) - 1);
  title_[sizeof(title_) - 1] = '\0';
  page_idx_ = page_idx;
  tft_.setTextDatum(TL_DATUM);
}

void TdeckChrome::paint_battery(uint16_t bat_mv, bool bat_usb) {
  const int x = tft_.width() - kBatW;
  tft_.fillRect(x, 0, kBatW, kHdrH, kSurface);

  char bat[12];
  uint16_t bat_color = kText;
  if (bat_usb) {
    snprintf(bat, sizeof(bat), "USB");
    bat_color = kAccent;
  } else if (bat_mv > 0) {
    uint8_t pct = battery_percent(bat_mv);
    snprintf(bat, sizeof(bat), "%u%%", (unsigned)pct);
    if (pct < 10) {
      bat_color = kDanger;
    } else if (pct < 25) {
      bat_color = kWarn;
    }
    const int bar_x = x + 4;
    const int bar_y = kHdrH - 6;
    const int bar_w = kBatW - 8;
    tft_.drawRect(bar_x, bar_y, bar_w, 4, kDivider);
    const int fill_w = (int)(bar_w - 2) * pct / 100;
    if (fill_w > 0) {
      tft_.fillRect(bar_x + 1, bar_y + 1, fill_w, 2, kAccent);
    }
  } else {
    strncpy(bat, "--", sizeof(bat));
  }

  tft_.setTextColor(bat_color, kSurface);
  tft_.setTextDatum(TR_DATUM);
  tft_.drawString(bat, tft_.width() - 4, 4, kFontLabel);
  tft_.setTextDatum(TL_DATUM);

  bat_mv_ = bat_mv;
  bat_usb_ = bat_usb;
}

void TdeckChrome::paint_header(const char *title, int page_idx, uint16_t bat_mv,
                               bool bat_usb, bool force) {
  const bool title_changed =
      force || !header_ok_ || strcmp(title_, title) != 0 ||
      page_idx_ != page_idx;
  const bool bat_changed =
      force || !header_ok_ || bat_mv_ != bat_mv || bat_usb_ != bat_usb;

  if (!title_changed && !bat_changed) {
    return;
  }

  if (title_changed) {
    paint_title_bar(title, page_idx);
  }
  if (bat_changed) {
    paint_battery(bat_mv, bat_usb);
  }
  header_ok_ = true;
}

void TdeckChrome::paint_text(int x, int y, const char *text, uint8_t font,
                             uint16_t fg, uint16_t bg) {
  tft_.setTextColor(fg, bg);
  tft_.drawString(text, x, y, font);
}

void TdeckChrome::clear_body() {
  const int h = tft_.height() - kHdrH - kChromeBottom;
  tft_.fillRect(0, kHdrH, tft_.width(), h, kBg);
}

void TdeckChrome::paint_divider(int y) {
  tft_.drawFastHLine(4, y, tft_.width() - 8, kDivider);
}

void TdeckChrome::paint_section_label(int y, const char *label) {
  paint_text(4, y, label, kFontLabel, kTextMuted, kBg);
}

void TdeckChrome::paint_field(int y, const char *label, const char *value) {
  paint_text(4, y, label, kFontLabel, kTextMuted, kBg);
  tft_.setTextColor(kText, kBg);
  tft_.setTextDatum(TR_DATUM);
  tft_.drawString(value, tft_.width() - 4, y, kFontValue);
  tft_.setTextDatum(TL_DATUM);
}

void TdeckChrome::paint_field_icon(int y, StatusIcon icon, const char *label,
                                   const char *value) {
  constexpr int kIconX = 4;
  constexpr int kTextX = 22;
  draw_status_icon(tft_, icon, kIconX, y, kTextMuted, kBg);
  paint_text(kTextX, y, label, kFontLabel, kTextMuted, kBg);
  tft_.setTextColor(kText, kBg);
  tft_.setTextDatum(TR_DATUM);
  tft_.drawString(value, tft_.width() - 4, y, kFontValue);
  tft_.setTextDatum(TL_DATUM);
}

void TdeckChrome::paint_chrome_bottom() {
  const int dot_y = tft_.height() - kFooterH - kDotBandH / 2;
  tft_.fillRect(0, tft_.height() - kChromeBottom, tft_.width(), kDotBandH,
                kBg);
  (void)dot_y;
}

void TdeckChrome::paint_soft_key_label(int x, int y, uint8_t datum,
                                       const char *label, char hotkey) {
  if (!label || !label[0]) return;

  tft_.setTextColor(kAccent, kSurface);
  tft_.setTextFont(kFontLabel);

  int hot_idx = -1;
  if (hotkey) {
    for (int i = 0; label[i]; i++) {
      const char c = label[i];
      if (c == hotkey || c == (char)(hotkey ^ 32)) {
        hot_idx = i;
        break;
      }
    }
  }

  if (hot_idx < 0) {
    tft_.setTextDatum(datum);
    tft_.drawString(label, x, y, kFontLabel);
    tft_.setTextDatum(TL_DATUM);
    return;
  }

  char prefix[24];
  char suffix[24];
  size_t pre_len = (size_t)hot_idx;
  if (pre_len >= sizeof(prefix)) pre_len = sizeof(prefix) - 1;
  memcpy(prefix, label, pre_len);
  prefix[pre_len] = '\0';
  strncpy(suffix, label + hot_idx + 1, sizeof(suffix) - 1);
  suffix[sizeof(suffix) - 1] = '\0';

  const char hot[2] = {label[hot_idx], '\0'};
  const int total_w = tft_.textWidth(label, kFontLabel);
  int x0 = x;
  if (datum == TC_DATUM) {
    x0 = x - total_w / 2;
  } else if (datum == TR_DATUM) {
    x0 = x - total_w;
  }

  tft_.setTextDatum(TL_DATUM);
  int cx = x0;
  if (prefix[0]) {
    tft_.drawString(prefix, cx, y, kFontLabel);
    cx += tft_.textWidth(prefix, kFontLabel);
  }
  tft_.drawString(hot, cx, y, kFontLabel);
  const int hot_w = tft_.textWidth(hot, kFontLabel);
  tft_.drawFastHLine(cx, y + 9, hot_w, kAccent);
  cx += hot_w;
  if (suffix[0]) {
    tft_.drawString(suffix, cx, y, kFontLabel);
  }
  tft_.setTextDatum(TL_DATUM);
}

void TdeckChrome::paint_soft_keys(const char *left, char left_key,
                                  const char *center, char center_key,
                                  const char *right, char right_key) {
  const int y = tft_.height() - kFooterH;
  tft_.fillRect(0, y, tft_.width(), kFooterH, kSurface);
  tft_.drawFastHLine(0, y, tft_.width(), kDivider);

  const int text_y = y + 12;
  if (left && left[0]) {
    paint_soft_key_label(4, text_y, TL_DATUM, left, left_key);
  }
  if (center && center[0]) {
    paint_soft_key_label(tft_.width() / 2, text_y, TC_DATUM, center,
                         center_key);
  }
  if (right && right[0]) {
    paint_soft_key_label(tft_.width() - 4, text_y, TR_DATUM, right,
                         right_key);
  }
}

void TdeckChrome::paint_page_dots(size_t count, size_t active) {
  if (count == 0) return;
  constexpr int kGap = 10;
  const int y = tft_.height() - kFooterH - kDotBandH / 2;
  const int active_r = 3;
  const int idle_r = 2;
  const int step = active_r * 2 + kGap;
  const int total_w = (int)count * step - kGap;
  int x = (tft_.width() - total_w) / 2 + active_r;
  tft_.fillRect(0, y - active_r - 2, tft_.width(), active_r * 2 + 5, kBg);
  for (size_t i = 0; i < count; i++) {
    if (i == active) {
      tft_.fillCircle(x, y, active_r, kAccent);
    } else {
      tft_.drawCircle(x, y, idle_r, kAccentDim);
    }
    x += step;
  }
}

void TdeckChrome::paint_list_row(int y, bool selected, bool flock_accent,
                                 const char *line1, uint8_t font1,
                                 const char *line2, uint8_t icon,
                                 bool has_icon) {
  const uint16_t bg = selected ? kSurfaceAlt : kBg;
  const int row_h = kRowH - 1;
  tft_.fillRect(0, y, tft_.width(), row_h, bg);
  if (selected) {
    const uint16_t bar = flock_accent ? kFlock : kAccent;
    tft_.fillRect(0, y, kAccentW, row_h, bar);
  }
  tft_.drawFastHLine(0, y + row_h - 1, tft_.width(), kDivider);

  int text_x = 4 + (selected ? kAccentW : 0);
  if (has_icon) {
    const DevIcon dev = static_cast<DevIcon>(icon);
    const uint16_t icon_fg = selected ? kAccent : kTextMuted;
    draw_dev_icon(tft_, dev, text_x, y + 10, icon_fg, bg);
    text_x += 18;
  }
  paint_text(text_x, y + 4, line1, font1, kText, bg);
  if (line2 && line2[0]) {
    paint_text(text_x, y + 18, line2, kFontLabel, kTextMuted, bg);
  }
}

void TdeckChrome::paint_scroll_list(size_t count, size_t *sel,
                                    size_t *paint_sel, size_t *paint_start,
                                    size_t *paint_count, ScrollRowFn row_fn,
                                    int top_y, bool force) {
  const int visible = list_visible_rows(top_y);

  if (count == 0) {
    if (force) {
      clear_body();
      paint_text(4, top_y + 8, "Nothing seen yet", kFontLabel, kTextMuted, kBg);
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
  if (count > (size_t)visible && *sel >= (size_t)visible) {
    start = *sel - (size_t)visible + 1;
  }

  auto paint_row = [&](size_t index, size_t row_index, bool selected) {
    char line1[20];
    char line2[48];
    bool flock = false;
    row_fn(index, line1, sizeof(line1), line2, sizeof(line2), &flock);
    const int y = top_y + (int)row_index * kRowH;
    paint_list_row(y, selected, flock, line1, kFontMac, line2);
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
    return;
  }

  if (force || start != *paint_start || *sel != *paint_sel ||
      count != *paint_count) {
    clear_body();
    for (size_t i = start; i < count && (int)(i - start) < visible; i++) {
      paint_row(i, i - start, i == *sel);
    }
    *paint_sel = *sel;
    *paint_start = start;
    *paint_count = count;
  }
}

void TdeckChrome::paint_icon_scroll_list(
    size_t count, size_t *sel, size_t *paint_sel, size_t *paint_start,
    size_t *paint_count, IconRowFn row_fn, int top_y, bool force) {
  const int visible = list_visible_rows(top_y);

  if (count == 0) {
    if (force) {
      clear_body();
      paint_text(4, top_y + 8, "Nothing seen yet", kFontLabel, kTextMuted, kBg);
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
  if (count > (size_t)visible && *sel >= (size_t)visible) {
    start = *sel - (size_t)visible + 1;
  }

  auto paint_row = [&](size_t index, size_t row_index, bool selected) {
    IconRow row{};
    row_fn(index, &row);
    const int y = top_y + (int)row_index * kRowH;
    paint_list_row(y, selected, false, row.line1, kFontLabel, row.line2,
                   row.icon, true);
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
    return;
  }

  if (force || start != *paint_start || *sel != *paint_sel ||
      count != *paint_count) {
    clear_body();
    for (size_t i = start; i < count && (int)(i - start) < visible; i++) {
      paint_row(i, i - start, i == *sel);
    }
    *paint_sel = *sel;
    *paint_start = start;
    *paint_count = count;
  }
}

#endif  // FD_ENABLE_TDECK_UI
