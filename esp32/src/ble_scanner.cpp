#include "ble_scanner.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

#include "match.h"
#include "protocol.h"

// "02:00:00:00:00:01" -> 6 bytes. Returns false on malformed input.
static bool parse_mac(const char *s, uint8_t out[6]) {
  for (int i = 0; i < 6; i++) {
    char *end = nullptr;
    long v = strtol(s, &end, 16);
    if (end == s || v < 0 || v > 0xFF) return false;
    out[i] = (uint8_t)v;
    s = end;
    if (i < 5) {
      if (*s != ':') return false;
      s++;
    }
  }
  return true;
}

class FlockScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *dev) override {
    if (!g_det_queue) return;

    Detection d;
    det_init(d, DET_BLE);
    d.ts_ms = millis();
    d.rssi = dev->getRSSI();
    d.has_rssi = true;
    d.has_mac = parse_mac(dev->getAddress().toString().c_str(), d.mac);

    if (dev->haveName()) {
      strncpy(d.name, dev->getName().c_str(), sizeof(d.name) - 1);
      d.has_name = d.name[0] != '\0';
    }

    // Manufacturer ID: first two bytes of manufacturer-specific data,
    // little-endian. mfgrid 2504 catches Penguin devices even when they drop
    // the "Penguin-" name prefix.
    bool flock = false;
    if (dev->haveManufacturerData()) {
      std::string md = dev->getManufacturerData();
      if (md.size() >= 2) {
        uint16_t cid = (uint8_t)md[0] | ((uint16_t)(uint8_t)md[1] << 8);
        if (mfgrid_is_flock(cid)) {
          d.mfgrid = cid;
          d.has_mfgrid = true;
          strncpy(d.method, "mfgrid", sizeof(d.method) - 1);
          flock = true;
        }
      }
    }

    // Name match (FS Ext Battery, flock, pigvision, ...) when not already
    // identified by manufacturer ID.
    if (!flock && d.has_name && ble_name_match(d.name)) {
      strncpy(d.method, "name_match", sizeof(d.method) - 1);
      flock = true;
    }

    if (flock && d.has_mac) xQueueSend(g_det_queue, &d, 0);
  }
};

void ble_scanner_begin() {
  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new FlockScanCallbacks(), /*wantDup=*/false);
  scan->setActiveScan(false);  // passive — don't send scan requests
  scan->setInterval(100);
  scan->setWindow(99);
  scan->setMaxResults(0);  // callback-only, don't buffer results
  scan->start(0, nullptr, false);  // scan forever
}
