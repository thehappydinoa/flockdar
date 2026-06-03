#include "lora_scanner.h"
#ifdef FD_ENABLE_LORA

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include "tdeck_board.h"

namespace {

SX1262 s_radio = new Module(
    TDECK_RADIO_CS, TDECK_RADIO_DIO1, TDECK_RADIO_RST, TDECK_RADIO_BUSY);

bool s_ok = false;
MeshNode s_nodes[kMaxMeshNodes];
size_t s_node_count = 0;
uint32_t s_packets_total = 0;
uint32_t s_events = 0;

// Meshtastic LongFast US915 primary channel parameters
constexpr float kFreqMHz    = 906.875f;
constexpr float kBwKHz      = 250.0f;
constexpr uint8_t kSf       = 11;
constexpr uint8_t kCr       = 5;   // 4/5 coding rate
constexpr uint8_t kSyncWord = 0x2B; // Meshtastic private
constexpr int8_t  kPower    = 17;   // dBm (RX only — ignored in receive)
constexpr uint16_t kPreamble = 16;

void upsert_node(uint32_t node_id, uint8_t channel, int16_t rssi, float snr) {
  // Find existing entry
  for (size_t i = 0; i < s_node_count; i++) {
    if (s_nodes[i].node_id == node_id) {
      s_nodes[i].rssi = rssi;
      s_nodes[i].snr  = snr;
      s_nodes[i].last_ms = millis();
      s_nodes[i].count++;
      s_nodes[i].channel_hash = channel;
      return;
    }
  }
  // New node
  s_events++;
  if (s_node_count < kMaxMeshNodes) {
    MeshNode &n = s_nodes[s_node_count++];
    n.node_id      = node_id;
    n.channel_hash = channel;
    n.rssi         = rssi;
    n.snr          = snr;
    n.last_ms      = millis();
    n.count        = 1;
  } else {
    // Replace oldest entry
    size_t oldest = 0;
    for (size_t i = 1; i < s_node_count; i++) {
      if (s_nodes[i].last_ms < s_nodes[oldest].last_ms) oldest = i;
    }
    s_nodes[oldest] = { node_id, channel, rssi, snr, millis(), 1 };
  }
}

}  // namespace

void lora_scanner_begin() {
  // Use the shared SPI bus (already initialised by TFT)
  // Deassert all other CS lines first
  tdeck_spi_idle();

  int state = s_radio.begin(kFreqMHz, kBwKHz, kSf, kCr, kSyncWord, kPower, kPreamble);
  if (state != RADIOLIB_ERR_NONE) {
    s_ok = false;
    return;
  }
  s_radio.setCRC(2);       // Meshtastic uses 2-byte CRC
  s_radio.startReceive();
  s_ok = true;
}

void lora_scanner_loop() {
  if (!s_ok) return;
  // Poll DIO1 — goes HIGH when a packet is ready
  if (!digitalRead(TDECK_RADIO_DIO1)) return;

  // Ensure other SPI devices are deasserted before RadioLib transaction
  tdeck_spi_idle();

  size_t pktLen = s_radio.getPacketLength();
  if (pktLen == 0) pktLen = 256;
  if (pktLen > 256) pktLen = 256;

  uint8_t buf[256];
  int state = s_radio.readData(buf, pktLen);
  s_radio.startReceive();  // restart immediately

  if (state != RADIOLIB_ERR_NONE) return;
  if (pktLen < 16) return;  // Meshtastic header is 16 bytes minimum

  // Parse Meshtastic raw packet header (not protobuf — raw binary on radio)
  // Bytes 0-3:  destination node ID (little-endian)
  // Bytes 4-7:  source (from) node ID (little-endian)
  // Bytes 8-11: packet ID (little-endian)
  // Byte 12:    flags (hop_limit:3, want_ack:1, via_mqtt:1, hop_start:3)
  // Byte 13:    channel hash
  // Bytes 14-15: reserved
  // Bytes 16+:  encrypted payload
  uint32_t from_id = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                     ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
  uint8_t channel  = buf[13];
  int16_t rssi     = (int16_t)s_radio.getRSSI();
  float   snr      = s_radio.getSNR();

  s_packets_total++;
  upsert_node(from_id, channel, rssi, snr);
}

bool lora_scanner_ok()             { return s_ok; }
size_t lora_nodes_count()          { return s_node_count; }
uint32_t lora_packets_total()      { return s_packets_total; }
uint32_t lora_sightings_events()   { return s_events; }

bool lora_nodes_get(size_t index, MeshNode *out) {
  if (!out || index >= s_node_count) return false;
  *out = s_nodes[index];
  return true;
}

#endif  // FD_ENABLE_LORA
