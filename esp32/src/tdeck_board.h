// tdeck_board.h — LilyGO T-Deck / T-Deck Plus pin map (factory utilities.h).
#pragma once

#include <stdint.h>

#define TDECK_POWERON 10
#define TDECK_I2C_SDA 18
#define TDECK_I2C_SCL 8
#define TDECK_KB_ADDR 0x55

#define TDECK_TFT_CS 12
#define TDECK_TFT_DC 11
#define TDECK_BL_PIN 42

#define TDECK_SPI_SCK 40
#define TDECK_SPI_MOSI 41
#define TDECK_SPI_MISO 38

#define TDECK_RADIO_CS 9
#define TDECK_SD_CS 39  // BOARD_SDCARD_CS — shared SPI with TFT + LoRa

#define TDECK_TBOX_RIGHT 2   // BOARD_TBOX_G02
#define TDECK_TBOX_UP 3      // BOARD_TBOX_G01
#define TDECK_TBOX_LEFT 1    // BOARD_TBOX_G04
#define TDECK_TBOX_DOWN 15   // BOARD_TBOX_G03
#define TDECK_TRACKBALL_BTN 0  // BOARD_BOOT_PIN (trackball click)

#define TDECK_BAT_ADC 4
// 100k/100k divider; Meshtastic t-deck variant uses ×2.11 for display correction.
#define TDECK_BAT_ADC_MULT 2.11f

// MAX98357A I2S speaker amp (LilyGO utilities.h: WS 5 / BCK 7 / DOUT 6).
// NOTE: the pin_spec T-Deck board still lists a vestigial OLED_SDA=7 (the
// T-Deck has no SSD1306 and uses FD_ENABLE_TDECK_UI, so FD_OLED_SDA is never
// consumed) — GPIO7 is the I2S bit-clock here.
#define TDECK_I2S_WS 5
#define TDECK_I2S_BCK 7
#define TDECK_I2S_DOUT 6

// Deassert every device on the shared SPI bus (LilyGO setupSD() pattern).
inline void tdeck_spi_idle() {
  digitalWrite(TDECK_TFT_CS, HIGH);
  digitalWrite(TDECK_RADIO_CS, HIGH);
  digitalWrite(TDECK_SD_CS, HIGH);
}
