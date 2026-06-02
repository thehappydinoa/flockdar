// audio.cpp — see audio.h. I2S glue for the T-Deck MAX98357A speaker.
#include "audio.h"

#if defined(FD_ENABLE_AUDIO)

#include <Arduino.h>
#include <driver/i2s.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tdeck_board.h"

namespace {

constexpr i2s_port_t kPort = I2S_NUM_0;
// Longest cue (HIGH = 90+40+90+40+140 = 400 ms) fits comfortably.
constexpr size_t kMaxSamples = TONE_SAMPLE_RATE * 1 / 2;  // 0.5 s @ 16 kHz

bool s_ready = false;
volatile bool s_muted = false;
QueueHandle_t s_queue = nullptr;  // holds ToneCue by value
TaskHandle_t s_task = nullptr;

void install_i2s() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = TONE_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = TDECK_I2S_BCK;
  pins.ws_io_num = TDECK_I2S_WS;
  pins.data_out_num = TDECK_I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) return;
  i2s_set_pin(kPort, &pins);
  i2s_zero_dma_buffer(kPort);
  s_ready = true;
}

void play_cue(const ToneCue &cue) {
  static int16_t buf[kMaxSamples];
  const size_t n = cue_render(&cue, buf, kMaxSamples);
  if (n == 0) return;
  size_t written = 0;
  i2s_write(kPort, buf, n * sizeof(int16_t), &written, portMAX_DELAY);
  // Flush tail and silence the amp so it doesn't idle-hiss.
  i2s_zero_dma_buffer(kPort);
}

void audio_task(void *) {
  ToneCue cue;
  for (;;) {
    if (xQueueReceive(s_queue, &cue, portMAX_DELAY) == pdTRUE) {
      if (!s_muted && s_ready) play_cue(cue);
    }
  }
}

}  // namespace

void audio_begin() {
  if (s_queue) return;  // already initialised
  install_i2s();
  // Depth 1: we intentionally drop overlapping requests rather than queue a
  // backlog of beeps when detections arrive in bursts.
  s_queue = xQueueCreate(1, sizeof(ToneCue));
  if (s_queue) {
    xTaskCreatePinnedToCore(audio_task, "fd_audio", 3072, nullptr, 1, &s_task, 0);
  }
}

void audio_play(const ToneCue &cue) {
  if (!s_queue || s_muted) return;
  // Non-blocking, no overwrite: if a cue is already pending/playing, skip.
  xQueueSend(s_queue, &cue, 0);
}

void audio_alert(uint8_t confidence) {
  if (s_muted) return;
  ToneCue cue = cue_for_confidence(confidence);
  audio_play(cue);
}

void audio_set_muted(bool muted) {
  s_muted = muted;
  if (muted && s_ready) i2s_zero_dma_buffer(kPort);
}

bool audio_muted() { return s_muted; }

#endif  // FD_ENABLE_AUDIO
