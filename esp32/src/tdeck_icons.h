// tdeck_icons.h — small device-type icons for the T-Deck UI.
#pragma once

#include <stddef.h>
#include <stdint.h>

class TFT_eSPI;

struct RfDevice;

enum class DevIcon : uint8_t {
  kUnknown = 0,
  kPhone,
  kWatch,
  kTv,
  kSpeaker,
  kCamera,
  kRouter,
  kComputer,
  kTracker,
  kBoot,  // trackball boot / power (UI chrome, not RF)
};

const char *dev_icon_label(DevIcon icon);
DevIcon classify_rf_device(const RfDevice &d, const char *vendor);
void draw_dev_icon(TFT_eSPI &tft, DevIcon icon, int x, int y, uint16_t fg,
                   uint16_t bg);
void draw_boot_icon(TFT_eSPI &tft, int x, int y, uint16_t fg, uint16_t bg);
