// display.h — optional on-device status UI.
//
//   -DFD_ENABLE_OLED      SSD1306 (generic dev board)
//   -DFD_ENABLE_TDECK_UI  LilyGO T-Deck / T-Deck Plus TFT + trackball + keyboard
#pragma once

#include "protocol.h"

#if defined(FD_ENABLE_OLED) || defined(FD_ENABLE_TDECK_UI)

void display_begin();
void display_note(const Detection &d);
void display_loop();

#endif
