// wifi_scanner.h — 802.11 promiscuous-mode Flock detection.
#pragma once

#include <stdint.h>

void wifi_scanner_begin();

// Advances the channel hopper (no-op when FD_FIXED_CHANNEL is set). Call from
// the main loop.
void wifi_scanner_loop();

// Current channel, for the OLED.
uint8_t wifi_scanner_channel();
