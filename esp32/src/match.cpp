#include "match.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "oui_list.h"    // generated: FLOCK_OUIS, FLOCK_MFGRIDS, FLOCK_BLE_NAMES
#include "vendor_list.h"  // generated: VENDOR_OUIS, BLE_VENDORS
#include "protocol.h"

bool oui_is_flock(const uint8_t mac[6]) {
  for (size_t i = 0; i < FLOCK_OUI_COUNT; i++) {
    if (mac[0] == FLOCK_OUIS[i][0] && mac[1] == FLOCK_OUIS[i][1] &&
        mac[2] == FLOCK_OUIS[i][2]) {
      return true;
    }
  }
  return false;
}

static bool oui_is_flock_direct(const uint8_t mac[6]) {
  // FLOCK_DIRECT_OUIS in signatures.py (currently b4:1e:52 only).
  return mac[0] == 0xB4 && mac[1] == 0x1E && mac[2] == 0x52;
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

bool flock_method_is_probe(const char *method) {
  return method && strcmp(method, "probe_request") == 0;
}

const char *flock_method_short(const char *method) {
  if (!method) return "?";
  if (strcmp(method, "probe_request") == 0) return "PROBE";
  if (strcmp(method, "addr2") == 0) return "OUI tx";
  if (strcmp(method, "addr1") == 0) return "OUI rx";
  if (strcmp(method, "name_match") == 0) return "BLE name";
  if (strcmp(method, "mfgrid") == 0) return "mfgrid";
  return method;
}

void flock_match_labels(DetKind kind, const char *method, const uint8_t *mac,
                        bool has_mac, const char *name, bool has_name,
                        uint16_t mfgrid, bool has_mfgrid, char *signal_out,
                        size_t signal_sz, char *detail_out, size_t detail_sz) {
  if (signal_out && signal_sz > 0) signal_out[0] = '\0';
  if (detail_out && detail_sz > 0) detail_out[0] = '\0';
  if (!signal_out || !detail_out || signal_sz == 0 || detail_sz == 0) return;

  if (kind == DET_WIFI && has_mac && method) {
    const bool direct = oui_is_flock_direct(mac);
    char oui[9];
    snprintf(oui, sizeof(oui), "%02x:%02x:%02x", mac[0], mac[1], mac[2]);
    if (direct) {
      strncpy(signal_out, "FLOCK_DIRECT_OUI", signal_sz - 1);
    } else {
      strncpy(signal_out, "CHIP_OUI", signal_sz - 1);
    }
    signal_out[signal_sz - 1] = '\0';

    const char *tier =
        direct ? "Flock Safety IEEE OUI" : "Flock chip vendor OUI";
    if (strcmp(method, "addr1") == 0) {
      snprintf(detail_out, detail_sz,
               "%s %s in frame receiver (addr1); likely sleeping camera",
               tier, oui);
    } else if (strcmp(method, "addr2") == 0) {
      snprintf(detail_out, detail_sz,
               "%s %s as frame transmitter (addr2); device actively sending",
               tier, oui);
    } else if (strcmp(method, "probe_request") == 0) {
      strncpy(signal_out, "WILDCARD_PROBE", signal_sz - 1);
      signal_out[signal_sz - 1] = '\0';
      snprintf(detail_out, detail_sz,
               "Empty-SSID probe request from %s %s; camera waking to upload",
               tier, oui);
    } else {
      snprintf(detail_out, detail_sz, "%s %s via %s", tier, oui, method);
    }
    return;
  }

  if (kind == DET_BLE && method) {
    if (strcmp(method, "name_match") == 0 && has_name && name) {
      const char *pat = ble_name_match(name);
      strncpy(signal_out, "BLE_NAME", signal_sz - 1);
      signal_out[signal_sz - 1] = '\0';
      if (pat) {
        snprintf(detail_out, detail_sz,
                 "BLE advertised name contains '%s' (matched '%s')", pat,
                 name);
      } else {
        snprintf(detail_out, detail_sz,
                 "BLE advertised name matched Flock pattern ('%s')", name);
      }
      return;
    }
    if (strcmp(method, "mfgrid") == 0 && has_mfgrid) {
      strncpy(signal_out, "FLOCK_MFGRID", signal_sz - 1);
      signal_out[signal_sz - 1] = '\0';
      snprintf(detail_out, detail_sz,
               "BLE manufacturer ID %u (Penguin / Flock, mfgrid 2504)",
               (unsigned)mfgrid);
      return;
    }
  }

  strncpy(signal_out, "FLOCK_MATCH", signal_sz - 1);
  signal_out[signal_sz - 1] = '\0';
  snprintf(detail_out, detail_sz, "Matched via %s",
           method ? method : "unknown");
}

void flock_match_summary(DetKind kind, const char *method, const uint8_t *mac,
                         bool has_mac, const char *name, bool has_name,
                         uint16_t mfgrid, bool has_mfgrid, char *summary_out,
                         size_t summary_sz) {
  if (!summary_out || summary_sz == 0) return;
  summary_out[0] = '\0';

  if (kind == DET_WIFI && has_mac && method) {
    const bool direct = oui_is_flock_direct(mac);
    const char *tier = direct ? "Flock" : "Chip";
    if (strcmp(method, "addr1") == 0) {
      snprintf(summary_out, summary_sz,
               "%s OUI in frame receiver; likely sleeping camera", tier);
    } else if (strcmp(method, "addr2") == 0) {
      snprintf(summary_out, summary_sz, "%s OUI actively transmitting", tier);
    } else if (strcmp(method, "probe_request") == 0) {
      snprintf(summary_out, summary_sz,
               "Empty-SSID probe; camera waking to upload");
    } else {
      snprintf(summary_out, summary_sz, "%s OUI matched via %s", tier, method);
    }
    return;
  }

  if (kind == DET_BLE && method) {
    if (strcmp(method, "name_match") == 0 && has_name && name) {
      const char *pat = ble_name_match(name);
      if (pat && ci_strstr(pat, "enguin")) {
        snprintf(summary_out, summary_sz, "Penguin BLE name pattern match");
      } else {
        snprintf(summary_out, summary_sz, "BLE name contains Flock pattern");
      }
      return;
    }
    if (strcmp(method, "mfgrid") == 0 && has_mfgrid) {
      snprintf(summary_out, summary_sz, "Penguin manufacturer ID %u",
               (unsigned)mfgrid);
      return;
    }
  }

  snprintf(summary_out, summary_sz, "Matched via %s",
           method ? method : "unknown");
}

uint8_t flock_det_confidence(const char *kind, const char *method,
                              const char *name, bool has_name) {
  if (flock_method_is_probe(method)) return 3;
  if (method && strcmp(method, "addr2") == 0) return 3;
  if (method && strcmp(method, "addr1") == 0) return 2;
  if (method && strcmp(method, "mfgrid") == 0) return 2;
  if (method && strcmp(method, "name_match") == 0 && has_name && name) {
    const char *pat = ble_name_match(name);
    if (pat && ci_strstr(pat, "enguin")) return 3;
    return 2;
  }
  if (kind && strcmp(kind, "ble") == 0) return 2;
  return 1;
}

const char *flock_confidence_label(uint8_t level) {
  switch (level) {
  case 3:
    return "HIGH";
  case 2:
    return "MED";
  default:
    return "LOW";
  }
}
