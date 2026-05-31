// tdeck_theme.h — tactical / BlackBerry palette and layout tokens (RGB565).
#pragma once

#include <stdint.h>

namespace TdeckTheme {

// Warm charcoal body — not pure black.
constexpr uint16_t kBg = 0x2125;         // #252528
constexpr uint16_t kSurface = 0x3187;    // #323238
constexpr uint16_t kSurfaceAlt = 0x3996; // #3a3530
constexpr uint16_t kText = 0xE71B;       // #e6e2d9
constexpr uint16_t kTextMuted = 0x8C50;  // #8a8580
constexpr uint16_t kDivider = 0x4A48;    // #4a4844
constexpr uint16_t kAccent = 0xD502;     // #d4a017 amber
constexpr uint16_t kAccentDim = 0x8B88;  // #8a7040
constexpr uint16_t kFlock = 0xE584;      // #e8b020
constexpr uint16_t kWarn = 0xFEA0;       // yellow
constexpr uint16_t kDanger = 0xF800;     // red

constexpr int kHeaderH = 28;
constexpr int kFooterH = 28;
constexpr int kDotBandH = 8;
constexpr int kChromeBottom = kFooterH + kDotBandH;  // 36
constexpr int kSoftKeyTextY = 9;  // offset within footer band
constexpr int kBodyTop = kHeaderH;
constexpr int kRowH = 36;
constexpr int kAccentW = 3;
constexpr int kFieldH = 14;
constexpr int kSectionH = 12;
constexpr int kLabelColW = 90;

constexpr uint8_t kFontLabel = 1;
constexpr uint8_t kFontValue = 1;
constexpr uint8_t kFontMac = 2;
constexpr uint8_t kFontTitle = 2;

}  // namespace TdeckTheme
