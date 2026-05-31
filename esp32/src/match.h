// match.h — signature matching against the auto-generated tables in
// oui_list.h (FLOCK_OUIS, FLOCK_MFGRIDS, FLOCK_BLE_NAMES).
#pragma once

#include <stddef.h>
#include <stdint.h>

// True if the first three octets of mac are a known Flock OUI.
bool oui_is_flock(const uint8_t mac[6]);

// True if cid is a known Flock BLE manufacturer (company) ID.
bool mfgrid_is_flock(uint16_t cid);

// Case-insensitive substring match of name against the BLE name patterns.
// Returns the matched pattern (for the JSON detail) or nullptr.
const char *ble_name_match(const char *name);

// Known vendor name from MAC OUI (Apple, Google, Samsung, …) or nullptr.
const char *oui_vendor_name(const uint8_t mac[6]);

// Known vendor from BLE manufacturer ID, or nullptr.
const char *ble_vendor_name(uint16_t cid);

// Confidence tier 1=LOW 2=MEDIUM 3=HIGH (mirrors flockdar detect.py).
uint8_t flock_det_confidence(const char *kind, const char *method,
                              const char *name, bool has_name);
const char *flock_confidence_label(uint8_t level);
bool flock_method_is_probe(const char *method);
const char *flock_method_short(const char *method);
