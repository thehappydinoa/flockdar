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
