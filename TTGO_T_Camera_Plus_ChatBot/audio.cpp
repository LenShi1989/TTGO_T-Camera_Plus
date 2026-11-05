// ============================================================================
//  audio.cpp - I2S microphone (32 bit stereo frames) -> 16 bit mono WAV
//
//  The TTGO T-Camera Plus uses an ICS-43434 style digital microphone wired
//  to I2S_NUM_1:  BCLK=14, WS=32, DATA=33.  Frames arrive as 32 bit words
//  (24 bit audio left aligned) in stereo, so the two slots are summed and
//  shifted right by 13 to obtain a 16 bit sample, matching the reference
//  firmware in esp32-camera-screen-master.
// ============================================================================
#include "audio.h"

#include <driver/i2s.h>
#include <FS.h>
#include <SD.h>

#include "board_config.h"
#include "storage.h"

static volatile int s_level = 0;

int audio_level() { return s_level; }

// ---------------------------------------------------------------------------
//  WAV header
// ---------------------------------------------------------------------------
static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static void put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void build_wav_header(uint8_t *h, uint32_t data_bytes, uint32_t rate) {
  memcpy(h + 0, "RIFF", 4);
  put_u32(h + 4, 36 + data_bytes);
  memcpy(h + 8, "WAVEfmt ", 8);
  put_u32(h + 16, 16);                 // PCM chunk size
  put_u16(h + 20, 1);                  // PCM
  put_u16(h + 22, 1);                  // mono
  put_u32(h + 24, rate);
  put_u32(h + 28, rate * 2);           // byte rate
  put_u16(h + 32, 2);                  // block align
  put_u16(h + 34, 16);                 // bits per sample
  memcpy(h + 36, "data", 4);
  put_u32(h + 40, data_bytes);
}

// ---------------------------------------------------------------------------
//  recording
// ---------------------------------------------------------------------------
bool audio_record_wav(const String &path, int seconds, int sample_rate,
                      int gain_percent, AudioProgressFn progress, String &err) {
  err = "";
  if (seconds < 1) seconds = 1;
  if (seconds > 20) seconds = 20;
  if (sample_rate < 8000) sample_rate = 16000;
  if (gain_percent < 25) gain_percent = 25;
  if (gain_percent > 400) gain_percent = 400;

  const size_t total_frames = (size_t)sample_rate * (size_t)seconds;
  int16_t *samples = (int16_t *)ps_malloc(total_frames * sizeof(int16_t));
  if (!samples) samples = (int16_t *)malloc(total_frames * sizeof(int16_t));
  if (!samples) {
    err = "記憶體不足，無法配置錄音緩衝區";
    return false;
  }

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = sample_rate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  cfg.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  cfg.bits_per_chan  = I2S_BITS_PER_CHAN_32BIT;
#else
  cfg.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_I2S;
#endif
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = false;
  cfg.fixed_mclk = 0;

  esp_err_t res = i2s_driver_install(I2S_NUM_1, &cfg, 0, nullptr);
  if (res != ESP_OK) {
    free(samples);
    err = "I2S 初始化失敗";
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.bck_io_num   = BOARD_MIC_BCLK;
  pins.ws_io_num    = BOARD_MIC_WS;
  pins.data_out_num = -1;
  pins.data_in_num  = BOARD_MIC_DATA;
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;

  res = i2s_set_pin(I2S_NUM_1, &pins);
  if (res != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_1);
    free(samples);
    err = "I2S 腳位設定失敗";
    return false;
  }

  i2s_zero_dma_buffer(I2S_NUM_1);
  i2s_start(I2S_NUM_1);

  const int chunk_frames = 256;
  int32_t raw[chunk_frames * 2];

  // discard ~180 ms so the first samples are not a click
  int warm_chunks = (sample_rate / chunk_frames) * 180 / 1000;
  if (warm_chunks < 1) warm_chunks = 1;
  for (int i = 0; i < warm_chunks; ++i) {
    size_t got = 0;
    i2s_read(I2S_NUM_1, raw, sizeof(raw), &got, pdMS_TO_TICKS(300));
  }

  size_t filled = 0;
  int    timeouts = 0;
  uint32_t last_report = 0;

  while (filled < total_frames) {
    size_t bytes_read = 0;
    esp_err_t r = i2s_read(I2S_NUM_1, raw, sizeof(raw), &bytes_read,
                           pdMS_TO_TICKS(600));
    if (r != ESP_OK || bytes_read == 0) {
      if (++timeouts > 40) break;
      continue;
    }
    timeouts = 0;

    size_t got = bytes_read / 8;   // 2 channels x 4 bytes
    if (got > (size_t)chunk_frames) got = chunk_frames;

    int peak = 0;
    for (size_t i = 0; i < got && filled < total_frames; ++i) {
      int64_t sum = (int64_t)raw[i * 2] + (int64_t)raw[i * 2 + 1];
      int32_t v = (int32_t)(sum >> 13);
      int64_t g = (int64_t)v * gain_percent / 100;
      if (g > 32767)  g = 32767;
      if (g < -32768) g = -32768;
      int16_t s = (int16_t)g;
      samples[filled++] = s;
      int a = (s < 0) ? -(int)s : (int)s;
      if (a > peak) peak = a;
    }

    int lv = (int)((int64_t)peak * 140 / 32768);
    if (lv > 100) lv = 100;
    s_level = lv;

    if (progress && millis() - last_report > 180) {
      last_report = millis();
      progress((int)(filled * 100 / total_frames), "錄音中");
    }
  }

  i2s_stop(I2S_NUM_1);
  i2s_driver_uninstall(I2S_NUM_1);
  s_level = 0;

  if (filled < total_frames / 2) {
    free(samples);
    err = "麥克風沒有收到足夠的音訊";
    return false;
  }

  uint32_t data_bytes = (uint32_t)filled * 2;
  uint8_t header[44];
  build_wav_header(header, data_bytes, (uint32_t)sample_rate);

  // Make sure the target directory exists before taking the bus lock.
  String parent = path;
  int slash = parent.lastIndexOf('/');
  if (slash > 0) {
    parent = parent.substring(0, slash);
    String derr;
    sd_ensure_dir(parent, derr);
  }

  if (progress) progress(100, "寫入音檔");

  String norm;
  if (!sd_norm(path, norm)) {
    free(samples);
    err = "錄音檔路徑不合法";
    return false;
  }

  if (!bus_lock(6000)) {
    free(samples);
    err = "SD 卡忙碌中，無法儲存錄音";
    return false;
  }

  bool ok = true;
  File f = SD.open(norm, "w");
  if (!f) {
    err = "無法建立錄音檔";
    ok = false;
  } else {
    size_t w1 = f.write(header, sizeof(header));
    size_t w2 = f.write((const uint8_t *)samples, data_bytes);
    f.close();
    if (w1 != sizeof(header) || w2 != data_bytes) {
      err = "錄音檔寫入失敗（SD 卡已滿？）";
      ok = false;
    }
  }

  bus_unlock();
  free(samples);
  return ok;
}
