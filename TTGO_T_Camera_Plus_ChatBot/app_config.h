// ============================================================================
//  app_config.h - persistent configuration stored in /sd/config.json
// ============================================================================
#ifndef TTGO_T_CAMERA_PLUS_APP_CONFIG_H
#define TTGO_T_CAMERA_PLUS_APP_CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>

struct AppConfig {
  // device
  String device_name;
  String ap_password;

  // station wifi
  String wifi_ssid;
  String wifi_password;

  // AI provider
  String ai_mode;          // "openai" (OpenAI compatible) or "ollama"
  String ai_base_url;      // e.g. https://api.openai.com/v1
  String ai_api_key;
  String ai_model;
  String ai_system_prompt;
  float  ai_temperature;
  int    ai_max_tokens;
  int    ai_timeout_ms;
  int    ai_history;       // remembered exchanges

  // speech to text
  bool   stt_enabled;
  String stt_base_url;     // empty -> derived from ai_base_url
  String stt_api_key;      // empty -> ai_api_key
  String stt_model;
  String stt_language;

  // microphone
  int    mic_sample_rate;
  int    mic_seconds;
  int    mic_gain;         // percent, 100 = unity

  // display
  int    brightness;       // 5..255
  int    rotation;         // 0..3
  bool   face_enabled;
};

// --- lifecycle -------------------------------------------------------------
void config_defaults(AppConfig &cfg);
bool config_begin();                                  // load + create defaults
bool config_save(String &err);                        // write to SD (SPIFFS fallback)
bool config_apply_json(JsonVariantConst src, String &err, bool persist);

// --- access ---------------------------------------------------------------
void   config_snapshot(AppConfig &out);               // thread safe copy
String config_to_json(bool pretty);
String config_device_slug();                          // mDNS / AP safe name
bool   config_set_wifi(const String &ssid, const String &password, String &err);

#endif  // TTGO_T_CAMERA_PLUS_APP_CONFIG_H
