#include "tdeck_icons.h"

#ifdef FD_ENABLE_TDECK_UI

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <TFT_eSPI.h>

#include "rf_sightings.h"

namespace {

bool ci_contains(const char *hay, const char *needle) {
  if (!hay || !needle || !needle[0]) return false;
  size_t nlen = strlen(needle);
  for (const char *h = hay; *h; h++) {
    size_t i = 0;
    while (i < nlen && h[i] &&
           tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) {
      i++;
    }
    if (i == nlen) return true;
  }
  return false;
}

bool vendor_is(const char *vendor, const char *name) {
  return vendor && name && strcmp(vendor, name) == 0;
}

bool vendor_is_router(const char *vendor) {
  if (!vendor) return false;
  static const char *kRouters[] = {
      "eero",     "Meraki",   "Cisco",    "Linksys",  "Netgear",  "TP-Link",
      "Ubiquiti", "ASUS",     "Arris",    "Motorola", "Verizon",  "Technicolor",
      "D-Link",   "Belkin",   "Ruckus",   "Aruba",    "Fortinet", "Huawei",
      "Sagemcom", "ZTE",      "Zyxel",    "Actiontec","Arcadyan", "AVM",
      "Askey",    "Hitron",   "Sercomm",  "2Wire",    "Compal",   "Pegatron",
      "Buffalo",  "Extreme",  "MikroTik", "Sky",      "Freebox",  "Tenda",
      "Aerohive", "Gemtek",   "NEC",      "Thomson",  "Mitsumi",  "HPE",
      "HP",       "Wistron",  nullptr,
  };
  for (const char **p = kRouters; *p; p++) {
    if (vendor_is(vendor, *p)) return true;
  }
  return false;
}

bool label_is_phone(const char *label) {
  if (!label || !label[0] || strcmp(label, "ble") == 0) return false;
  static const char *kPhone[] = {
      "iphone",   "ipad",     "android",  "galaxy",   "pixel",
      "oneplus",  "redmi",    "poco",     "motorola", "moto g",
      "moto-",    "nokia",    "oppo",     "vivo",     "realme",
      "honor",    "phone",    "mobile",   "sm-",      "sm_a",
      "sm-g",     "sm-s",     "sm-n",     "xperia",   nullptr,
  };
  for (const char **p = kPhone; *p; p++) {
    if (ci_contains(label, *p)) return true;
  }
  return false;
}

bool vendor_is_camera(const char *vendor) {
  if (!vendor) return false;
  static const char *kCams[] = {
      "Hikvision", "Dahua",  "Reolink", "Wyze",   "Ring",     "Axis",
      "Avigilon",  "FLIR",   "Hanwha",  "GeoVision", "March", "Mobotix",
      "Sunell",    "Bosch",  "Pelco",   "Vivotek", "Uniview",  nullptr,
  };
  for (const char **p = kCams; *p; p++) {
    if (vendor_is(vendor, *p)) return true;
  }
  return false;
}

}  // namespace

const char *dev_icon_label(DevIcon icon) {
  switch (icon) {
  case DevIcon::kPhone:
    return "phone";
  case DevIcon::kWatch:
    return "watch";
  case DevIcon::kTv:
    return "tv";
  case DevIcon::kSpeaker:
    return "speaker";
  case DevIcon::kCamera:
    return "camera";
  case DevIcon::kRouter:
    return "router";
  case DevIcon::kComputer:
    return "computer";
  case DevIcon::kTracker:
    return "tracker";
  case DevIcon::kBoot:
    return "boot";
  default:
    return "device";
  }
}

DevIcon classify_rf_device(const RfDevice &d, const char *vendor) {
  const bool ble = strcmp(d.kind, "ble") == 0;
  const char *label = d.label;

  if (ble) {
    if (ci_contains(label, "watch") || ci_contains(label, "band") ||
        ci_contains(label, "fitbit")) {
      return DevIcon::kWatch;
    }
    if (ci_contains(label, "airpod") || ci_contains(label, "buds") ||
        ci_contains(label, "headphone") || ci_contains(label, "beats") ||
        ci_contains(label, "speaker") || ci_contains(label, "sonos")) {
      return DevIcon::kSpeaker;
    }
    if (ci_contains(label, "tv") || ci_contains(label, "roku") ||
        ci_contains(label, "fire tv")) {
      return DevIcon::kTv;
    }
    if (ci_contains(label, "tile") || ci_contains(label, "tracker") ||
        ci_contains(label, "tag")) {
      return DevIcon::kTracker;
    }
    if (ci_contains(label, "camera") || ci_contains(label, "cam")) {
      return DevIcon::kCamera;
    }
    if (d.has_mfgrid && d.mfgrid == 737) return DevIcon::kTracker;
    if (vendor_is(vendor, "Sonos") || vendor_is(vendor, "Bose")) {
      return DevIcon::kSpeaker;
    }
    if (vendor_is(vendor, "Garmin") || vendor_is(vendor, "Fitbit")) {
      return DevIcon::kWatch;
    }
    if (label_is_phone(label)) {
      return DevIcon::kPhone;
    }
    return DevIcon::kUnknown;
  }

  if (vendor_is_camera(vendor)) return DevIcon::kCamera;
  if (vendor_is(vendor, "Roku")) return DevIcon::kTv;
  if (vendor_is(vendor, "Sonos") || vendor_is(vendor, "Bose")) {
    return DevIcon::kSpeaker;
  }
  if (vendor_is(vendor, "Synology") || vendor_is(vendor, "Intel") ||
      vendor_is(vendor, "Microsoft") || vendor_is(vendor, "HP") ||
      vendor_is(vendor, "HPE")) {
    return DevIcon::kComputer;
  }
  if (vendor_is_router(vendor)) return DevIcon::kRouter;
  return DevIcon::kUnknown;
}

void draw_boot_icon(TFT_eSPI &tft, int x, int y, uint16_t fg, uint16_t bg) {
  tft.fillRect(x, y, 16, 16, bg);
  tft.drawCircle(x + 8, y + 9, 5, fg);
  tft.drawLine(x + 8, y + 1, x + 8, y + 5, fg);
}

void draw_flockdar_logo(TFT_eSPI &tft, int x, int y, uint16_t fg, uint16_t bg) {
  draw_dev_icon(tft, DevIcon::kCamera, x, y, fg, bg);
}

void draw_status_icon(TFT_eSPI &tft, StatusIcon icon, int x, int y, uint16_t fg,
                      uint16_t bg) {
  // 12×12 glyph in the 14×14 status field cell, top-aligned with text.
  const int ox = x + 1;
  const int oy = y;

  switch (icon) {
  case StatusIcon::kFlock:
    tft.fillRect(x, y, 14, 14, bg);
    tft.fillRect(ox + 4, oy + 1, 4, 2, fg);
    tft.fillRoundRect(ox + 1, oy + 3, 10, 7, 1, fg);
    tft.drawCircle(ox + 6, oy + 6, 2, bg);
    return;
  case StatusIcon::kWifi:
    tft.fillRect(x, y, 14, 14, bg);
    tft.drawFastHLine(ox + 2, oy + 9, 8, fg);
    tft.drawPixel(ox + 5, oy + 8, fg);
    tft.drawPixel(ox + 6, oy + 8, fg);
    tft.drawCircle(ox + 6, oy + 6, 4, fg);
    tft.drawCircle(ox + 6, oy + 6, 1, bg);
    return;
  case StatusIcon::kBle:
    tft.fillRect(x, y, 14, 14, bg);
    tft.drawFastVLine(ox + 4, oy + 2, 8, fg);
    tft.fillTriangle(ox + 4, oy + 2, ox + 8, oy + 4, ox + 4, oy + 6, fg);
    tft.fillTriangle(ox + 4, oy + 6, ox + 8, oy + 8, ox + 4, oy + 10, fg);
    tft.fillTriangle(ox + 4, oy + 2, ox + 6, oy + 4, ox + 4, oy + 6, bg);
    tft.fillTriangle(ox + 4, oy + 6, ox + 6, oy + 8, ox + 4, oy + 10, bg);
    return;
  case StatusIcon::kChannel:
    tft.fillRect(x, y, 14, 14, bg);
    tft.fillRoundRect(ox + 1, oy + 5, 10, 5, 1, fg);
    tft.drawFastVLine(ox + 3, oy + 2, 3, fg);
    tft.drawFastVLine(ox + 6, oy + 1, 4, fg);
    tft.drawFastVLine(ox + 9, oy + 2, 3, fg);
    return;
  case StatusIcon::kGps:
    tft.fillRect(x, y, 14, 14, bg);
    tft.drawCircle(ox + 6, oy + 6, 5, fg);
    tft.drawFastHLine(ox + 3, oy + 6, 7, fg);
    tft.drawFastVLine(ox + 6, oy + 3, 7, fg);
    tft.fillCircle(ox + 6, oy + 6, 1, fg);
    return;
  case StatusIcon::kSd:
    tft.fillRect(x, y, 14, 14, bg);
    tft.fillRoundRect(ox + 2, oy + 1, 8, 10, 1, fg);
    tft.fillTriangle(ox + 2, oy + 1, ox + 5, oy + 1, ox + 2, oy + 4, bg);
    tft.drawFastHLine(ox + 3, oy + 6, 6, bg);
    return;
  default:
    tft.fillRect(x, y, 14, 14, bg);
    break;
  }
}

void draw_dev_icon(TFT_eSPI &tft, DevIcon icon, int x, int y, uint16_t fg,
                   uint16_t bg) {
  tft.fillRect(x, y, 14, 14, bg);
  switch (icon) {
  case DevIcon::kPhone:
    tft.fillRoundRect(x + 3, y + 1, 8, 12, 2, fg);
    tft.drawFastHLine(x + 5, y + 11, 4, bg);
    break;
  case DevIcon::kWatch:
    tft.fillRect(x + 1, y + 4, 3, 6, fg);
    tft.fillRect(x + 10, y + 4, 3, 6, fg);
    tft.fillRoundRect(x + 4, y + 3, 6, 8, 1, fg);
    tft.drawPixel(x + 6, y + 6, bg);
    break;
  case DevIcon::kTv:
    tft.fillRect(x + 2, y + 2, 10, 7, fg);
    tft.fillTriangle(x + 5, y + 10, x + 9, y + 10, x + 7, y + 12, fg);
    break;
  case DevIcon::kSpeaker:
    tft.fillRect(x + 2, y + 4, 3, 6, fg);
    tft.fillTriangle(x + 5, y + 3, x + 5, y + 11, x + 10, y + 7, fg);
    tft.drawFastVLine(x + 11, y + 4, 6, fg);
    tft.drawFastVLine(x + 12, y + 5, 4, fg);
    break;
  case DevIcon::kCamera:
    tft.fillRoundRect(x + 2, y + 4, 10, 7, 1, fg);
    tft.fillRect(x + 5, y + 2, 4, 3, fg);
    tft.drawCircle(x + 7, y + 7, 2, bg);
    break;
  case DevIcon::kRouter:
    tft.fillRoundRect(x + 2, y + 6, 10, 5, 1, fg);
    tft.drawFastVLine(x + 4, y + 2, 4, fg);
    tft.drawFastVLine(x + 7, y + 1, 5, fg);
    tft.drawFastVLine(x + 10, y + 2, 4, fg);
    break;
  case DevIcon::kComputer:
    tft.fillRect(x + 2, y + 3, 10, 6, fg);
    tft.fillRect(x + 1, y + 10, 12, 2, fg);
    break;
  case DevIcon::kTracker:
    tft.fillCircle(x + 7, y + 7, 4, fg);
    tft.drawCircle(x + 7, y + 7, 1, bg);
    break;
  case DevIcon::kBoot:
    draw_boot_icon(tft, x - 1, y - 1, fg, bg);
    break;
  default:
    tft.drawCircle(x + 7, y + 7, 3, fg);
    tft.drawPixel(x + 7, y + 7, bg);
    break;
  }
}

#endif  // FD_ENABLE_TDECK_UI
