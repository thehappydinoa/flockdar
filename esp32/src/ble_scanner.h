// ble_scanner.h — NimBLE passive advertisement scanner for Flock devices.
#pragma once

#include <stdint.h>

void ble_scanner_begin();
// BLE advertisements heard (all devices, not just Flock).
uint32_t ble_scanner_adverts();
