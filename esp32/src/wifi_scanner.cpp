#include "wifi_scanner.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#include "esp_wifi.h"

#include "config.h"
#include "flock_dedup.h"
#include "match.h"
#include "protocol.h"
#include "stats.h"

#ifdef FD_ENABLE_TDECK_UI
#include "rf_pending.h"
#endif

// 2.4 GHz primaries — the only channels Flock cameras are seen on.
static const uint8_t HOP_CHANNELS[] = {1, 6, 11};
static const size_t HOP_COUNT = sizeof(HOP_CHANNELS) / sizeof(HOP_CHANNELS[0]);

static volatile uint8_t s_channel = 1;
static size_t s_hop_idx = 0;
static uint32_t s_last_hop = 0;
static volatile uint32_t s_mgmt_frames = 0;
static bool s_suspended = false;

// 802.11 management frame subtypes we care about.
static const uint8_t SUBTYPE_PROBE_REQ = 0x04;

// enqueue_wifi is called exclusively from promisc_cb (WiFi task context).
// IRAM_ATTR keeps the function resident in RAM so it remains accessible when
// the flash instruction cache is briefly disabled (e.g. during flash writes).
static IRAM_ATTR void enqueue_wifi(const char *method, const uint8_t mac[6],
                                   int rssi, uint8_t channel) {
  if (!flock_dedup_allow(mac, method)) {
    return;
  }
  if (!g_det_queue) {
    return;
  }
  Detection d;
  det_init(d, DET_WIFI);
  strncpy(d.method, method, sizeof(d.method) - 1);
  memcpy(d.mac, mac, 6);
  d.has_mac = true;
  d.emit_oui = true;
  d.rssi = rssi;
  d.has_rssi = true;
  d.channel = channel;
  d.has_channel = true;
  d.ts_ms = millis();
  if (xQueueSend(g_det_queue, &d, 0) != pdTRUE) {
    stats_note_queue_drop();
  }
}

// Extract SSID from 802.11 Information Elements starting at ie_start.
// Returns true and fills ssid_out (NUL-terminated, max ssid_sz-1 chars)
// if a non-hidden SSID IE is found. Safe to call from IRAM context.
static IRAM_ATTR bool parse_ssid_ie(const uint8_t *frame, int frame_len,
                                     int ie_start, char *ssid_out,
                                     size_t ssid_sz) {
  if (ie_start + 2 > frame_len) return false;
  const uint8_t *ie = frame + ie_start;
  const int ie_len = frame_len - ie_start;
  int off = 0;
  while (off + 1 < ie_len) {
    const uint8_t id = ie[off];
    const uint8_t el = ie[off + 1];
    if (off + 2 + el > ie_len) break;
    if (id == 0) {
      if (el == 0) return false;  // hidden / wildcard
      const size_t n = (el < ssid_sz - 1) ? (size_t)el : ssid_sz - 1;
      for (size_t i = 0; i < n; i++) {
        const uint8_t c = ie[off + 2 + i];
        if (c < 0x20 || c > 0x7e) return false;  // non-ASCII → skip
      }
      memcpy(ssid_out, &ie[off + 2], n);
      ssid_out[n] = '\0';
      return true;
    }
    off += 2 + el;
  }
  return false;
}

static void IRAM_ATTR promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  s_mgmt_frames++;
  const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
  const uint8_t *pl = pkt->payload;
  const int len = pkt->rx_ctrl.sig_len;
  if (len < 24) return;  // shorter than an 802.11 MAC header

  const uint8_t fc0 = pl[0];
  const uint8_t ftype = (fc0 >> 2) & 0x3;
  const uint8_t fsub = (fc0 >> 4) & 0xF;
  if (ftype != 0) return;  // management frames only

  const uint8_t *addr1 = pl + 4;   // receiver
  const uint8_t *addr2 = pl + 10;  // transmitter
  const int rssi = pkt->rx_ctrl.rssi;
  const uint8_t ch = pkt->rx_ctrl.channel;

  // Extract SSID from beacon (8) and probe response (5) frames.
  // Both frame types have 12 bytes of fixed parameters after the 24-byte
  // MAC header, so Information Elements start at offset 36.
  char ssid[33] = {};
  bool has_ssid = false;
  if ((fsub == 8 || fsub == 5) && len >= 38) {
    has_ssid = parse_ssid_ie(pl, len, 36, ssid, sizeof(ssid));
  }

  // Skip multicast and locally-administered (randomized) WiFi MACs — no stable
  // OUI to label; AP/camera BSSIDs use universal addresses.
#ifdef FD_ENABLE_TDECK_UI
  if ((addr2[0] & 0x03) == 0) {
    rf_pending_note_wifi(addr2, rssi, ch, has_ssid ? ssid : nullptr);
  }
#endif

  // 1+2. addr2 transmitter — a Flock device actively sending a frame.
  if (oui_is_flock(addr2)) enqueue_wifi("addr2", addr2, rssi, ch);

  // 3. addr1 receiver — catches sleeping cameras as the destination of
  //    probe responses from nearby APs. Skip multicast / locally-administered
  //    receiver addresses (never a real device MAC).
  if (!(addr1[0] & 0x01) && !(addr1[0] & 0x02) && oui_is_flock(addr1)) {
    enqueue_wifi("addr1", addr1, rssi, ch);
  }

  // 4. Wildcard probe request (SSID tag id 0, length 0) from a Flock device
  //    waking to upload. Gated on the transmitter OUI so we don't flag the
  //    wildcard probes every client device sends.
  if (fsub == SUBTYPE_PROBE_REQ && len >= 26 && oui_is_flock(addr2)) {
    const uint8_t tag_id = pl[24];
    const uint8_t tag_len = pl[25];
    if (tag_id == 0 && tag_len == 0) {
      enqueue_wifi("probe_request", addr2, rssi, ch);
    }
  }
}

static void set_channel(uint8_t ch) {
  s_channel = ch;
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

void wifi_scanner_begin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  esp_wifi_set_promiscuous(true);

  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&promisc_cb);

#ifdef FD_FIXED_CHANNEL
  set_channel(FD_FIXED_CHANNEL);
#else
  set_channel(HOP_CHANNELS[0]);
  s_last_hop = millis();
#endif
}

void wifi_scanner_suspend() {
  if (s_suspended) {
    return;
  }
  esp_wifi_set_promiscuous(false);
  s_suspended = true;
}

void wifi_scanner_resume() {
  if (!s_suspended) {
    return;
  }
  esp_wifi_set_promiscuous(true);
  s_suspended = false;
}

void wifi_scanner_loop() {
  if (s_suspended) {
    return;
  }
#ifndef FD_FIXED_CHANNEL
  uint32_t now = millis();
  if (now - s_last_hop >= FD_CHANNEL_DWELL_MS) {
    s_last_hop = now;
    s_hop_idx = (s_hop_idx + 1) % HOP_COUNT;
    set_channel(HOP_CHANNELS[s_hop_idx]);
  }
#endif
}

uint8_t wifi_scanner_channel() { return s_channel; }

uint32_t wifi_scanner_mgmt_frames() { return s_mgmt_frames; }
