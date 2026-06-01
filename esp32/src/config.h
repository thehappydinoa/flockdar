// config.h — compile-time configuration for flockdar-esp32.
//
// Most knobs can be overridden from platformio.ini build_flags (e.g.
// -DFD_HMAC_KEY=\"...\", -DFD_FIXED_CHANNEL=6) without editing this file.
#pragma once

#include <stdint.h>

// Validated, board-conditional GPIO assignments (generated from
// esp32/pin_spec.py). Provides FD_OLED_SDA/SCL, FD_SD_CS/SCK/MISO/MOSI,
// FD_GPS_RX_PIN/TX_PIN, FD_GPS_UART. See esp32/HARDWARE.md.
#include "pins.h"

// --- Protocol / serial -----------------------------------------------------
#define FD_PROTO_VERSION 1
#define FD_SERIAL_BAUD 115200
// Bump when flashing to confirm the new build is running (shown on boot + status).
#ifndef FD_FW_VERSION
#define FD_FW_VERSION "0.2.5"
#endif

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
// Enable with -DFD_ENABLE_OLED / -DFD_ENABLE_TDECK_UI / -DFD_ENABLE_GPS
// (see env:esp32-s3-full, env:t-deck).

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

// microSD: pins come from pins.h (FD_SD_CS/SCK/MISO/MOSI). Behaviour only here.
#ifdef FD_ENABLE_SD
#ifndef FD_SD_FLUSH_MS
#define FD_SD_FLUSH_MS 3000  // flush the log to card at most this often
#endif
#endif  // FD_ENABLE_SD

// GPS: pins/UART come from pins.h (FD_GPS_RX_PIN/TX_PIN, FD_GPS_UART).
#ifdef FD_ENABLE_GPS
#ifndef FD_GPS_BAUD
#define FD_GPS_BAUD 9600
#endif
#ifndef FD_GPS_EMIT_INTERVAL_MS
#define FD_GPS_EMIT_INTERVAL_MS 5000
#endif
#ifndef FD_GPS_STATUS_INTERVAL_MS
#define FD_GPS_STATUS_INTERVAL_MS 10000  // unsigned gps_status lines for serial debug
#endif
#endif  // FD_ENABLE_GPS
