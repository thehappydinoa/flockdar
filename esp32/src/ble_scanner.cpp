#include "ble_scanner.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

#include "match.h"
#include "protocol.h"
#include "rf_sightings.h"

namespace {

static volatile uint32_t s_ble_adverts = 0;

// Parse BLE AD structures without std::string / heap churn in the scan callback.
static bool ad_walk(const uint8_t *payload, size_t len,
                    bool (*fn)(uint8_t type, const uint8_t *data, uint8_t data_len,
                               void *ctx),
                    void *ctx) {
  size_t off = 0;
  while (off + 1 < len) {
    const uint8_t field_len = payload[off];
    if (field_len == 0 || off + field_len >= len) break;
    const uint8_t type = payload[off + 1];
    const uint8_t *data = &payload[off + 2];
    const uint8_t data_len = field_len - 1;
    if (fn(type, data, data_len, ctx)) return true;
    off += field_len + 1;
  }
  return false;
}

struct BleParseCtx {
  Detection *d;
  bool flock;
};

static bool ble_ad_parse(uint8_t type, const uint8_t *data, uint8_t data_len,
                         void *ctx) {
  auto *c = static_cast<BleParseCtx *>(ctx);
  Detection &d = *c->d;

  if ((type == 0x08 || type == 0x09) && data_len > 0 && !d.has_name) {
    size_t n = data_len;
    if (n >= sizeof(d.name)) n = sizeof(d.name) - 1;
    memcpy(d.name, data, n);
    d.name[n] = '\0';
    d.has_name = d.name[0] != '\0';
    if (!c->flock && d.has_name && ble_name_match(d.name)) {
      strncpy(d.method, "name_match", sizeof(d.method) - 1);
      c->flock = true;
    }
    return false;
  }

  if (type == 0xFF && data_len >= 2 && !c->flock) {
    const uint16_t cid =
        (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    if (mfgrid_is_flock(cid)) {
      d.mfgrid = cid;
      d.has_mfgrid = true;
      strncpy(d.method, "mfgrid", sizeof(d.method) - 1);
      c->flock = true;
    }
  }
  return false;
}

class FlockScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *dev) override {
    s_ble_adverts++;

    Detection d;
    det_init(d, DET_BLE);
    d.ts_ms = millis();
    d.rssi = dev->getRSSI();
    d.has_rssi = true;

    const uint8_t *mac = dev->getAddress().getNative();
    if (mac) {
      memcpy(d.mac, mac, 6);
      d.has_mac = true;
    }

    uint8_t *payload = dev->getPayload();
    const size_t pay_len = dev->getPayloadLength();
    BleParseCtx ctx{&d, false};
    if (payload && pay_len > 0) {
      ad_walk(payload, pay_len, ble_ad_parse, &ctx);
    }

    if (d.has_mac) {
      rf_sightings_note_ble(d.mac, d.has_name ? d.name : nullptr, d.rssi);
    }

    if (!g_det_queue || !ctx.flock || !d.has_mac) return;
    xQueueSend(g_det_queue, &d, 0);
  }
};

FlockScanCallbacks s_ble_cb;

}  // namespace

void ble_scanner_begin() {
  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&s_ble_cb, /*wantDup=*/false);
  scan->setActiveScan(false);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->setMaxResults(0);
  scan->start(0, nullptr, false);
}

uint32_t ble_scanner_adverts() { return s_ble_adverts; }
