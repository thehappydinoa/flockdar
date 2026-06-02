// tdeck_ui.h — LilyGO T-Deck on-device UI (enable with -DFD_ENABLE_TDECK_UI).
#pragma once

#include "protocol.h"

#ifdef FD_ENABLE_TDECK_UI

void tdeck_ui_begin();
void tdeck_ui_note(const Detection &d);
void tdeck_ui_loop();
// Advance the boot progress bar. Call after each init step (pct 0-100).
void tdeck_boot_step(const char *label, uint8_t pct);
// Release TFT SPI so SD / other bus devices can transact.
void tdeck_spi_release();

// Capture TFT over USB serial (RGB565). Non-blocking: begin then poll from loop.
bool tdeck_screenshot_begin();
void tdeck_screenshot_poll();
bool tdeck_screenshot_busy();

#ifdef FD_ENABLE_SD
// Mount microSD on the shared SPI bus (LilyGO setupSD pattern).
// hard_reset: SPI.end/begin + tft re-init (use on manual retry).
bool tdeck_mount_sd(bool hard_reset, char *err, size_t err_len,
                    uint32_t *speed_hz, uint8_t *card_type, int *miso_level);
#endif

#endif
