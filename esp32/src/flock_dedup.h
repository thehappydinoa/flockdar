// flock_dedup.h — short TTL dedup before enqueueing Flock detections.
#pragma once

#include <stdint.h>

// Returns false if the same (mac, method) was enqueued within FD_FLOCK_DEDUP_MS.
// Placed in IRAM (see flock_dedup.cpp) to stay accessible when the flash
// cache is briefly stalled by the WiFi promiscuous callback.
bool flock_dedup_allow(const uint8_t mac[6], const char *method);
