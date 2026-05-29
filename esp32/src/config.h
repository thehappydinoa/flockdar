// config.h — compile-time configuration for flockdar-esp32.
//
// Most knobs can be overridden from platformio.ini build_flags (e.g.
// -DFD_HMAC_KEY=\"...\", -DFD_FIXED_CHANNEL=6) without editing this file.
#pragma once

#include <stdint.h>

// --- Protocol / serial -----------------------------------------------------
#define FD_PROTO_VERSION 1
#define FD_SERIAL_BAUD 115200

// HMAC-SHA256 key shared with the Python receiver so it can reject forged or
// corrupted frames. Override via build_flags; this default is for bench use.
#ifndef FD_HMAC_KEY
#define FD_HMAC_KEY "flockdar-dev-key"
#endif

// --- WiFi promiscuous scanning ---------------------------------------------
// Dwell time per channel when hopping (ms).
#ifndef FD_CHANNEL_DWELL_MS
#define FD_CHANNEL_DWELL_MS 800
#endif
// Define FD_FIXED_CHANNEL (1..13) to lock to a single channel instead of
// hopping the 2.4 GHz primaries. Leave undefined to hop 1 -> 6 -> 11.

// --- Optional peripherals --------------------------------------------------
// Enable with -DFD_ENABLE_OLED / -DFD_ENABLE_GPS (see env:esp32-s3-full).

#ifdef FD_ENABLE_OLED
#ifndef FD_OLED_ADDR
#define FD_OLED_ADDR 0x3C
#endif
#ifndef FD_OLED_WIDTH
#define FD_OLED_WIDTH 128
#endif
#ifndef FD_OLED_HEIGHT
#define FD_OLED_HEIGHT 64
#endif
#ifndef FD_OLED_REDRAW_MS
#define FD_OLED_REDRAW_MS 250
#endif
#endif  // FD_ENABLE_OLED

#ifdef FD_ENABLE_GPS
#ifndef FD_GPS_UART
#define FD_GPS_UART 1
#endif
#ifndef FD_GPS_RX_PIN
#define FD_GPS_RX_PIN 16
#endif
#ifndef FD_GPS_TX_PIN
#define FD_GPS_TX_PIN 17
#endif
#ifndef FD_GPS_BAUD
#define FD_GPS_BAUD 9600
#endif
#ifndef FD_GPS_EMIT_INTERVAL_MS
#define FD_GPS_EMIT_INTERVAL_MS 5000
#endif
#endif  // FD_ENABLE_GPS
