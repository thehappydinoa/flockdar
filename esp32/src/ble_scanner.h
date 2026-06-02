// ble_scanner.h — NimBLE passive advertisement scanner for Flock devices.
#pragma once

#include <stdint.h>

void ble_scanner_begin();
void ble_scanner_suspend();
void ble_scanner_resume();
// BLE advertisements heard (all devices, not just Flock).
uint32_t ble_scanner_adverts();
