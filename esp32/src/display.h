// display.h — optional SSD1306 OLED status (enable with -DFD_ENABLE_OLED).
#pragma once

#include "config.h"

#ifdef FD_ENABLE_OLED
#include "protocol.h"

void display_begin();
// Record a detection for the on-screen counter / last-MAC line.
void display_note(const Detection &d);
// Redraw the screen at the configured interval. Call from loop.
void display_loop();
#endif
