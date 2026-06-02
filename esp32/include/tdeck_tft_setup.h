// TFT_eSPI setup for LilyGO T-Deck / T-Deck Plus (Setup210_LilyGo_T_Deck.h).
// Included via PlatformIO: -include esp32/include/tdeck_tft_setup.h
#pragma once

#define USER_SETUP_ID 210
#define ST7789_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_RGB_ORDER TFT_RGB
#define INIT_SEQUENCE_2

#define TFT_MISO 38
#define TFT_MOSI 41
#define TFT_SCLK 40
#define TFT_CS 12
#define TFT_DC 11
#define TFT_RST -1
#define TFT_BACKLIGHT_ON 1

// No touch panel — T-Deck uses a trackball (silences TFT_eSPI TOUCH_CS warning).
#define TOUCH_CS -1

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY 40000000
// ST7789 readback is unreliable above ~6 MHz on many panels.
#define SPI_READ_FREQUENCY 6000000
