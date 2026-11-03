#pragma once
#include <Arduino.h>

// 所有參數都存放在 SD 卡 /config.json
#define APP_CONFIG_PATH "/config.json"

struct WifiCfg {
  String ssid;
  String password;
  uint32_t connect_timeout_ms = 15000;
  String ap_ssid = "TTGO-AI";
  String ap_password = "12345678";
};

// 預設端點；{model} 與 {key} 會在送出前替換，可改成自架 / 代理的相容服務。
#define AI_DEFAULT_URL \
  "https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent?key={key}"

struct AiCfg {
  String api_key;
  String api_url = AI_DEFAULT_URL;
  String model = "gemini-2.0-flash";
  String system_prompt;
  uint32_t timeout_ms = 45000;
  int max_output_tokens = 512;
  float temperature = 0.7f;
};

struct AudioCfg {
  bool enabled = true; // 板載 I2S 麥克風開關（網頁可切換）
  uint32_t sample_rate = 16000;
  uint32_t max_record_ms = 6000;
  uint32_t min_record_ms = 1200;
  uint32_t silence_ms = 800;
  int vad_threshold = 700;
  int gain = 1; // 1 = 原始音量, 2/4 = 放大
};

struct CameraCfg {
  int xclk_hz = 20000000;
  int framesize = 5; // FRAMESIZE_QVGA
  int jpeg_quality = 12;
  float rec_threshold = 0.55f;
  int enroll_samples = 3;
  int max_faces = 16;
  int vflip = 0;
  int hmirror = 0;
};

struct DisplayCfg {
  int brightness = 255; // 0..255
};

struct WebCfg {
  bool enabled = true;
  String user;
  String password;
};

struct AppConfig {
  WifiCfg wifi;
  AiCfg ai;
  AudioCfg audio;
  CameraCfg camera;
  DisplayCfg display;
  WebCfg web;
  String font_path = "/font.bin";
  String faces_path = "/faces.csv";

  // 從 SD 卡載入；若不存在則建立預設檔
  bool load();
  bool save();
  void resetDefaults();
  String toJson(bool maskSecrets = true) const;
  bool fromJson(const String &json, String *err = nullptr);
  bool valid() const { return !ai.api_key.isEmpty(); }
};

extern AppConfig g_cfg;
