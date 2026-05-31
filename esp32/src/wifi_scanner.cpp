#include "wifi_scanner.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#include "esp_wifi.h"

#include "config.h"
#include "match.h"
#include "protocol.h"

// 2.4 GHz primaries — the only channels Flock cameras are seen on.
static const uint8_t HOP_CHANNELS[] = {1, 6, 11};
static const size_t HOP_COUNT = sizeof(HOP_CHANNELS) / sizeof(HOP_CHANNELS[0]);

static volatile uint8_t s_channel = 1;
static size_t s_hop_idx = 0;
static uint32_t s_last_hop = 0;
static volatile uint32_t s_mgmt_frames = 0;

// 802.11 management frame subtypes we care about.
static const uint8_t SUBTYPE_PROBE_REQ = 0x04;

static void enqueue_wifi(const char *method, const uint8_t mac[6], int rssi,
                         uint8_t channel) {
  if (!g_det_queue) return;
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
  // Non-blocking: drop the frame rather than stall the WiFi task if full.
  xQueueSend(g_det_queue, &d, 0);
}

static void promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
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

void wifi_scanner_loop() {
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
