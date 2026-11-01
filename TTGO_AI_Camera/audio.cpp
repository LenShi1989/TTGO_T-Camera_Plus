#include "audio.h"

#include <driver/i2s.h>
#include <math.h>

#include "app_config.h"
#include "board_pins.h"
#include "util.h"

static bool s_ready = false;

bool audioReady() { return s_ready; }

bool audioBegin() {
  if (s_ready) return true;

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = g_cfg.audio.sample_rate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL2;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = false;
  cfg.fixed_mclk = 0;

  esp_err_t err = i2s_driver_install((i2s_port_t)I2S_MIC_PORT, &cfg, 0, NULL);
  if (err != ESP_OK) {
    logLine("MIC", "I2S 驅動安裝失敗 (%d)", (int)err);
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_MIC_BCK;
  pins.ws_io_num = PIN_MIC_WS;
  pins.data_out_num = -1;
  pins.data_in_num = PIN_MIC_DATA;

  err = i2s_set_pin((i2s_port_t)I2S_MIC_PORT, &pins);
  if (err != ESP_OK) {
    logLine("MIC", "I2S 腳位設定失敗 (%d)", (int)err);
    i2s_driver_uninstall((i2s_port_t)I2S_MIC_PORT);
    return false;
  }

  i2s_zero_dma_buffer((i2s_port_t)I2S_MIC_PORT);
  s_ready = true;
  logLine("MIC", "I2S 麥克風就緒 (%u Hz)", (unsigned)g_cfg.audio.sample_rate);
  return true;
}

void audioEnd() {
  if (!s_ready) return;
  i2s_driver_uninstall((i2s_port_t)I2S_MIC_PORT);
  s_ready = false;
}

int audioLevelOnce() {
  if (!s_ready) return 0;
  int32_t buf[128];
  size_t got = 0;
  if (i2s_read((i2s_port_t)I2S_MIC_PORT, buf, sizeof(buf), &got, 200 / portTICK_PERIOD_MS) !=
      ESP_OK) {
    return 0;
  }
  size_t n = got / sizeof(int32_t);
  if (n < 2) return 0;
  double sum = 0;
  for (size_t i = 0; i + 1 < n; i += 2) {
    int32_t v = (buf[i] >> 16) + (buf[i + 1] >> 16);
    sum += (double)v * v;
  }
  double rms = sqrt(sum / (double)(n / 2));
  int level = (int)(rms / 120.0);
  if (level > 100) level = 100;
  return level;
}

size_t audioRecord(int16_t *dst, size_t maxSamples, uint32_t maxMs, uint32_t minMs,
                   uint32_t silenceMs, int vadThreshold, int gain, AudioProgressFn progress) {
  if (!s_ready && !audioBegin()) return 0;

  const size_t kBlock = 256; // int32 slots（含左右聲道）
  int32_t raw[kBlock];

  size_t written = 0;
  uint32_t start = millis();
  uint32_t lastSound = start;
  uint32_t lastCb = 0;
  int peak = 0;

  while (written < maxSamples) {
    size_t got = 0;
    esp_err_t err = i2s_read((i2s_port_t)I2S_MIC_PORT, raw, sizeof(raw), &got,
                             500 / portTICK_PERIOD_MS);
    if (err != ESP_OK) break;
    size_t n = got / sizeof(int32_t);
    if (n < 2) continue;

    for (size_t i = 0; i + 1 < n && written < maxSamples; i += 2) {
      int32_t v = (raw[i] >> 16) + (raw[i + 1] >> 16);
      v *= gain;
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      int16_t s = (int16_t)v;
      dst[written++] = s;
      int a = s < 0 ? -s : s;
      if (a > peak) peak = a;
    }

    uint32_t now = millis();
    double sum = 0;
    size_t cnt = 0;
    for (size_t i = 0; i < n; i += 2) {
      int32_t v = ((raw[i] >> 16) + (raw[i + 1] >> 16)) * gain;
      sum += (double)v * v;
      cnt++;
    }
    double rms = cnt ? sqrt(sum / (double)cnt) : 0;
    if (rms > vadThreshold) lastSound = now;

    if (progress && now - lastCb >= 80) {
      lastCb = now;
      int level = (int)(rms / 120.0);
      if (level > 100) level = 100;
      progress(level, now - start);
    }

    if (now - start >= maxMs) break;
    if (now - start >= minMs && now - lastSound >= silenceMs) break;
  }

  // 音量太小就自動放大（上限 16 倍）
  if (peak > 0 && peak < 9000) {
    int boost = 20000 / peak;
    if (boost > 16) boost = 16;
    if (boost > 1) {
      for (size_t i = 0; i < written; i++) {
        int32_t v = (int32_t)dst[i] * boost;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        dst[i] = (int16_t)v;
      }
      logLine("MIC", "自動增益 x%d（峰值 %d）", boost, peak);
    }
  }

  logLine("MIC", "錄音完成：%u 樣本（%u ms）", (unsigned)written,
          (unsigned)(written * 1000UL / g_cfg.audio.sample_rate));
  return written;
}
