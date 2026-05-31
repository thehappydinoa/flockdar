#include "match.h"

#include <ctype.h>
#include <string.h>

#include "oui_list.h"    // generated: FLOCK_OUIS, FLOCK_MFGRIDS, FLOCK_BLE_NAMES
#include "vendor_list.h"  // generated: VENDOR_OUIS, BLE_VENDORS

bool oui_is_flock(const uint8_t mac[6]) {
  for (size_t i = 0; i < FLOCK_OUI_COUNT; i++) {
    if (mac[0] == FLOCK_OUIS[i][0] && mac[1] == FLOCK_OUIS[i][1] &&
        mac[2] == FLOCK_OUIS[i][2]) {
      return true;
    }
  }
  return false;
}

bool mfgrid_is_flock(uint16_t cid) {
  for (size_t i = 0; i < FLOCK_MFGRID_COUNT; i++) {
    if (FLOCK_MFGRIDS[i] == cid) return true;
  }
  return false;
}

// Lowercase, NUL-safe substring search.
static const char *ci_strstr(const char *haystack, const char *needle) {
  size_t nlen = strlen(needle);
  if (nlen == 0) return haystack;
  for (const char *h = haystack; *h; h++) {
    size_t i = 0;
    while (i < nlen && h[i] &&
           tolower((unsigned char)h[i]) == (unsigned char)needle[i]) {
      i++;
    }
    if (i == nlen) return h;
  }
  return nullptr;
}

const char *ble_name_match(const char *name) {
  if (!name || !name[0]) return nullptr;
  for (size_t i = 0; i < FLOCK_BLE_NAME_COUNT; i++) {
    // FLOCK_BLE_NAMES entries are already lowercase (from the generator).
    if (ci_strstr(name, FLOCK_BLE_NAMES[i])) return FLOCK_BLE_NAMES[i];
  }
  return nullptr;
}

static int cmp_vendor_oui(const void *a, const void *b) {
  const VendorOui *va = (const VendorOui *)a;
  const VendorOui *vb = (const VendorOui *)b;
  if (va->b0 != vb->b0) return (int)va->b0 - (int)vb->b0;
  if (va->b1 != vb->b1) return (int)va->b1 - (int)vb->b1;
  return (int)va->b2 - (int)vb->b2;
}

static int cmp_ble_vendor(const void *a, const void *b) {
  const BleVendor *va = (const BleVendor *)a;
  const BleVendor *vb = (const BleVendor *)b;
  return (int)va->cid - (int)vb->cid;
}

const char *oui_vendor_name(const uint8_t mac[6]) {
  VendorOui key = {mac[0], mac[1], mac[2], nullptr};
  const VendorOui *hit = (const VendorOui *)bsearch(
      &key, VENDOR_OUIS, VENDOR_OUI_COUNT, sizeof(VendorOui), cmp_vendor_oui);
  return hit ? hit->name : nullptr;
}

const char *ble_vendor_name(uint16_t cid) {
  BleVendor key = {cid, nullptr};
  const BleVendor *hit = (const BleVendor *)bsearch(
      &key, BLE_VENDORS, BLE_VENDOR_COUNT, sizeof(BleVendor), cmp_ble_vendor);
  return hit ? hit->name : nullptr;
}
