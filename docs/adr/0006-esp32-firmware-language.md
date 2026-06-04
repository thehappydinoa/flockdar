# ADR-0006: Retain C++ for ESP32 firmware

**Status:** Accepted  
**Date:** 2026-06-04

## Context

The host-side rewrite to Go (ADR-0001) raises the question of whether to also rewrite the ESP32 firmware in Rust via Embassy/esp-hal, which would theoretically allow code sharing between host and firmware.

## Decision

**Retain C++ (ESP-IDF / Arduino / PlatformIO)** for ESP32 firmware. Rust on ESP32 is tracked as a future option but not pursued now.

## Rationale

### ESP32 Rust ecosystem maturity

As of mid-2026, Rust on ESP32 via `esp-hal` and Embassy is functional for basic peripherals but has gaps relevant to this project:

- NimBLE (BLE stack) has no stable Rust binding; the C NimBLE library can be called via FFI but loses most safety benefits
- Simultaneous WiFi promiscuous mode + BLE is only reliably supported through ESP-IDF's C API
- SD card + SPI + GPS + BLE + WiFi simultaneously on ESP32-S3 is a known-working combination in C++; the Rust equivalent has open issues
- T-Deck display (ST7789 + LVGL) has a C++ driver; Rust LVGL bindings are incomplete

### What would be lost in a Rust rewrite

- Tested, working firmware with 6+ months of field use
- PlatformIO build system (convenient, well-documented)
- The `gen_oui_header.py` → `oui_list.h` pipeline (already language-agnostic, works either way)

### Code sharing is less valuable than it appears

The host and firmware share a wire protocol (ADR-0003) but not business logic — the firmware's job is capture + sign + emit, not detection. Detection runs on the host. There is no meaningful shared code between `internal/detect/` (Go) and the firmware beyond the NDJSON schema, which is already language-agnostic.

## Future consideration

If Embassy on ESP32 reaches maturity with stable NimBLE + WiFi promiscuous support, a Rust firmware rewrite becomes worth evaluating. The wire protocol (ADR-0003) is language-agnostic, so the host side does not need to change.

Track: https://github.com/esp-rs/esp-hal for status.

## Consequences

- ESP32 firmware stays in `esp32/` using PlatformIO
- `gen_oui_header.py` and `pin_spec.py` remain Python — they are build tools, not runtime
- Firmware CI: PlatformIO build check on each PR (no hardware required)
- No Rust toolchain required in developer setup for firmware work
