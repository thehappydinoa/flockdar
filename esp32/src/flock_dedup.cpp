#include "flock_dedup.h"

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "freertos/FreeRTOS.h"

namespace {

constexpr size_t kSlots = 16;

struct Slot {
  uint8_t mac[6];
  char method[16];
  uint32_t last_ms;
  bool used;
};

Slot s_slots[kSlots];
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

int find_slot(const uint8_t mac[6], const char *method) {
  for (size_t i = 0; i < kSlots; i++) {
    if (!s_slots[i].used) {
      continue;
    }
    if (memcmp(s_slots[i].mac, mac, 6) == 0 &&
        strncmp(s_slots[i].method, method, sizeof(s_slots[i].method)) == 0) {
      return (int)i;
    }
  }
  return -1;
}

int alloc_slot() {
  for (size_t i = 0; i < kSlots; i++) {
    if (!s_slots[i].used) {
      return (int)i;
    }
  }
  uint32_t oldest = UINT32_MAX;
  int idx = 0;
  for (size_t i = 0; i < kSlots; i++) {
    if (s_slots[i].last_ms < oldest) {
      oldest = s_slots[i].last_ms;
      idx = (int)i;
    }
  }
  return idx;
}

}  // namespace

bool flock_dedup_allow(const uint8_t mac[6], const char *method) {
  if (!mac || !method) {
    return true;
  }
  const uint32_t now = millis();
  // portENTER_CRITICAL_ISR is safe from both task and ISR context; the WiFi
  // promiscuous callback calls flock_dedup_allow() from an ISR-level context,
  // so portENTER_CRITICAL (which asserts !ISR) would panic.
  portENTER_CRITICAL_ISR(&s_mux);
  const int hit = find_slot(mac, method);
  if (hit >= 0) {
    if (now - s_slots[hit].last_ms < (uint32_t)FD_FLOCK_DEDUP_MS) {
      portEXIT_CRITICAL_ISR(&s_mux);
      return false;
    }
    s_slots[hit].last_ms = now;
    portEXIT_CRITICAL_ISR(&s_mux);
    return true;
  }
  const int idx = alloc_slot();
  s_slots[idx].used = true;
  memcpy(s_slots[idx].mac, mac, 6);
  strncpy(s_slots[idx].method, method, sizeof(s_slots[idx].method) - 1);
  s_slots[idx].method[sizeof(s_slots[idx].method) - 1] = '\0';
  s_slots[idx].last_ms = now;
  portEXIT_CRITICAL_ISR(&s_mux);
  return true;
}
