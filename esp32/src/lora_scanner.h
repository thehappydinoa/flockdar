// lora_scanner.h — passive Meshtastic/LoRa packet reception on the T-Deck SX1262.
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef FD_ENABLE_LORA

struct MeshNode {
  uint32_t node_id;       // Meshtastic from-node ID
  uint8_t  channel_hash;  // channel byte from packet header
  int16_t  rssi;          // dBm, last packet
  float    snr;           // dB, last packet
  uint32_t last_ms;       // millis() at last packet
  uint32_t count;         // packets received from this node
};

constexpr size_t kMaxMeshNodes = 32;

void lora_scanner_begin();
void lora_scanner_loop();    // call from main loop — non-blocking
bool lora_scanner_ok();
size_t lora_nodes_count();
bool lora_nodes_get(size_t index, MeshNode *out);
uint32_t lora_packets_total();
uint32_t lora_sightings_events();  // increments on each new unique node

#endif  // FD_ENABLE_LORA
