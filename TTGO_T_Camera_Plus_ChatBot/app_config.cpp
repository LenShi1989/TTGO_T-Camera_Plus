// ============================================================================
//  app_config.cpp - configuration load / save / validation
// ============================================================================
#include "app_config.h"

#include <FS.h>
#include <SPIFFS.h>
#include <SD.h>

#include "board_config.h"
#include "storage.h"

static AppConfig        s_cfg;
static SemaphoreHandle_t s_mutex = nullptr;

static void cfg_lock() {
  if (s_mutex) xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
}
static void cfg_unlock() {
  if (s_mutex) xSemaphoreGiveRecursive(s_mutex);
}

// ---------------------------------------------------------------------------
//  defaults
// ---------------------------------------------------------------------------
void config_defaults(AppConfig &c) {
  c.device_name = "T-Camera-Plus";
  c.ap_password = "12345678";

  c.wifi_ssid     = "";
  c.wifi_password = "";

  c.ai_mode      = "openai";
  c.ai_base_url  = "https://api.openai.com/v1";
  c.ai_api_key   = "";
  c.ai_model     = "gpt-4o-mini";
  c.ai_system_prompt =
      "你是住在這台 ESP32 小相機裡的 AI 助理，個性友善、有點幽默。"
      "一律使用繁體中文（台灣用語）回答，最多 80 個字，語氣口語化。"
      "不要使用 Markdown、標題、條列符號或表情符號，因為回覆會顯示在 240x240 的小螢幕上。";
  c.ai_temperature = 0.7f;
  c.ai_max_tokens  = 512;
  c.ai_timeout_ms  = 45000;
  c.ai_history     = 8;

  c.stt_enabled   = false;
  c.stt_base_url  = "";
  c.stt_api_key   = "";
  c.stt_model     = "whisper-1";
  c.stt_language  = "zh";

  c.mic_sample_rate = 16000;
  c.mic_seconds     = 6;
  c.mic_gain        = 100;

  c.brightness   = 200;
  c.rotation     = 0;
  c.face_enabled = true;
}

// ---------------------------------------------------------------------------
//  JSON helpers
// ---------------------------------------------------------------------------
static void copy_string(JsonVariantConst src, const char *key, String &dst, size_t max_len) {
  JsonVariantConst v = src[key];
  if (v.isNull()) return;
  String s = v.as<String>();
  s.trim();
  if (s.length() > max_len) s = s.substring(0, max_len);
  dst = s;
}

static void copy_int(JsonVariantConst src, const char *key, int &dst, int lo, int hi) {
  JsonVariantConst v = src[key];
  if (v.isNull()) return;
  long n = v.as<long>();
  if (n < lo) n = lo;
  if (n > hi) n = hi;
  dst = (int)n;
}

static void copy_float(JsonVariantConst src, const char *key, float &dst, float lo, float hi) {
  JsonVariantConst v = src[key];
  if (v.isNull()) return;
  float n = v.as<float>();
  if (n < lo) n = lo;
  if (n > hi) n = hi;
  dst = n;
}

static void copy_bool(JsonVariantConst src, const char *key, bool &dst) {
  JsonVariantConst v = src[key];
  if (v.isNull()) return;
  dst = v.as<bool>();
}

static void sanitize() {
  s_cfg.device_name.trim();
  if (s_cfg.device_name.length() == 0) s_cfg.device_name = "T-Camera-Plus";
  if (s_cfg.device_name.length() > 32) s_cfg.device_name = s_cfg.device_name.substring(0, 32);

  if (s_cfg.ap_password.length() < 8) s_cfg.ap_password = "12345678";
  if (s_cfg.ap_password.length() > 63) s_cfg.ap_password = s_cfg.ap_password.substring(0, 63);

  s_cfg.ai_mode.toLowerCase();
  if (s_cfg.ai_mode != "ollama") s_cfg.ai_mode = "openai";

  if (s_cfg.ai_base_url.length() == 0) {
    s_cfg.ai_base_url = (s_cfg.ai_mode == "ollama") ? "http://192.168.1.100:11434"
                                                    : "https://api.openai.com/v1";
  }

  if (s_cfg.ai_model.length() == 0) s_cfg.ai_model = "gpt-4o-mini";
  if (s_cfg.ai_system_prompt.length() == 0) {
    s_cfg.ai_system_prompt = "請一律使用繁體中文回答，回答要簡短、口語化，最多 80 個字。";
  }

  static const int kRates[] = {8000, 16000, 22050, 32000, 44100, 48000};
  bool rate_ok = false;
  for (int r : kRates) {
    if (s_cfg.mic_sample_rate == r) { rate_ok = true; break; }
  }
  if (!rate_ok) s_cfg.mic_sample_rate = 16000;

  if (s_cfg.stt_model.length() == 0)  s_cfg.stt_model = "whisper-1";
  if (s_cfg.stt_language.length() == 0) s_cfg.stt_language = "zh";
}

// ---------------------------------------------------------------------------
//  serialisation
// ---------------------------------------------------------------------------
static void to_doc(JsonDocument &doc) {
  doc["device_name"] = s_cfg.device_name;
  doc["ap_password"] = s_cfg.ap_password;

  doc["wifi_ssid"]     = s_cfg.wifi_ssid;
  doc["wifi_password"] = s_cfg.wifi_password;

  doc["ai_mode"]          = s_cfg.ai_mode;
  doc["ai_base_url"]      = s_cfg.ai_base_url;
  doc["ai_api_key"]       = s_cfg.ai_api_key;
  doc["ai_model"]         = s_cfg.ai_model;
  doc["ai_system_prompt"] = s_cfg.ai_system_prompt;
  doc["ai_temperature"]   = s_cfg.ai_temperature;
  doc["ai_max_tokens"]    = s_cfg.ai_max_tokens;
  doc["ai_timeout_ms"]    = s_cfg.ai_timeout_ms;
  doc["ai_history"]       = s_cfg.ai_history;

  doc["stt_enabled"]  = s_cfg.stt_enabled;
  doc["stt_base_url"] = s_cfg.stt_base_url;
  doc["stt_api_key"]  = s_cfg.stt_api_key;
  doc["stt_model"]    = s_cfg.stt_model;
  doc["stt_language"] = s_cfg.stt_language;

  doc["mic_sample_rate"] = s_cfg.mic_sample_rate;
  doc["mic_seconds"]     = s_cfg.mic_seconds;
  doc["mic_gain"]        = s_cfg.mic_gain;

  doc["brightness"]   = s_cfg.brightness;
  doc["rotation"]     = s_cfg.rotation;
  doc["face_enabled"] = s_cfg.face_enabled;
}

String config_to_json(bool pretty) {
  cfg_lock();
  DynamicJsonDocument doc(6144);
  to_doc(doc);
  String out;
  if (pretty) {
    serializeJsonPretty(doc, out);
  } else {
    serializeJson(doc, out);
  }
  cfg_unlock();
  return out;
}

void config_snapshot(AppConfig &out) {
  cfg_lock();
  out = s_cfg;
  cfg_unlock();
}

String config_device_slug() {
  cfg_lock();
  String src = s_cfg.device_name;
  cfg_unlock();

  src.trim();
  String out;
  out.reserve(src.length());
  for (size_t i = 0; i < src.length(); ++i) {
    char c = src[i];
    if (c >= 'A' && c <= 'Z') {
      out += (char)(c - 'A' + 'a');
    } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
      out += c;
    } else if (c == ' ' || c == '_' || c == '.') {
      out += '-';
    }
  }
  while (out.startsWith("-")) out.remove(0, 1);
  while (out.endsWith("-")) out.remove(out.length() - 1);
  if (out.length() == 0) out = "t-camera-plus";
  if (out.length() > 24) out = out.substring(0, 24);
  return out;
}

// ---------------------------------------------------------------------------
//  load / save
// ---------------------------------------------------------------------------
static int load_from_fs(fs::FS &fs, const char *path) {
  if (!fs.exists(path)) return 1;   // missing
  File f = fs.open(path, "r");
  if (!f) return -1;
  size_t size = f.size();
  if (size == 0 || size > CONFIG_MAX_BYTES) {
    f.close();
    Serial.printf("[cfg] %s has an invalid size (%u)\n", path, (unsigned)size);
    return -1;
  }
  String text;
  text.reserve(size + 1);
  while (f.available()) text += (char)f.read();
  f.close();

  DynamicJsonDocument doc(CONFIG_MAX_BYTES);
  DeserializationError e = deserializeJson(doc, text);
  if (e) {
    Serial.printf("[cfg] JSON error in %s: %s\n", path, e.c_str());
    return -1;
  }
  String err;
  if (!config_apply_json(doc.as<JsonVariantConst>(), err, false)) {
    Serial.printf("[cfg] invalid config %s: %s\n", path, err.c_str());
    return -1;
  }
  Serial.printf("[cfg] loaded %s (%u bytes)\n", path, (unsigned)size);
  return 0;
}

bool config_save(String &err) {
  String text = config_to_json(true);
  if (sd_ready()) {
    if (sd_write_text(CONFIG_FILE_SD, text, err)) {
      Serial.println("[cfg] saved to SD /config.json");
      return true;
    }
    Serial.printf("[cfg] SD save failed: %s\n", err.c_str());
  }

  File f = SPIFFS.open(CONFIG_FILE_SPIFFS, "w");
  if (!f) {
    err = "無法寫入設定檔（SD 與 SPIFFS 均失敗）";
    return false;
  }
  size_t written = f.print(text);
  f.close();
  if (written != text.length()) {
    err = "設定檔寫入不完整";
    return false;
  }
  Serial.println("[cfg] saved to SPIFFS /config.json");
  return true;
}

bool config_apply_json(JsonVariantConst src, String &err, bool persist) {
  err = "";
  if (!src.is<JsonObjectConst>()) {
    err = "設定必須是 JSON 物件";
    return false;
  }

  cfg_lock();

  copy_string(src, "device_name", s_cfg.device_name, 32);
  copy_string(src, "ap_password", s_cfg.ap_password, 63);

  copy_string(src, "wifi_ssid", s_cfg.wifi_ssid, 32);
  copy_string(src, "wifi_password", s_cfg.wifi_password, 64);

  copy_string(src, "ai_mode", s_cfg.ai_mode, 16);
  copy_string(src, "ai_base_url", s_cfg.ai_base_url, 200);
  copy_string(src, "ai_api_key", s_cfg.ai_api_key, 200);
  copy_string(src, "ai_model", s_cfg.ai_model, 80);
  copy_string(src, "ai_system_prompt", s_cfg.ai_system_prompt, 800);
  copy_float(src, "ai_temperature", s_cfg.ai_temperature, 0.0f, 2.0f);
  copy_int(src, "ai_max_tokens", s_cfg.ai_max_tokens, 32, 4096);
  copy_int(src, "ai_timeout_ms", s_cfg.ai_timeout_ms, 5000, 120000);
  copy_int(src, "ai_history", s_cfg.ai_history, 0, 30);

  copy_bool(src, "stt_enabled", s_cfg.stt_enabled);
  copy_string(src, "stt_base_url", s_cfg.stt_base_url, 200);
  copy_string(src, "stt_api_key", s_cfg.stt_api_key, 200);
  copy_string(src, "stt_model", s_cfg.stt_model, 80);
  copy_string(src, "stt_language", s_cfg.stt_language, 8);

  copy_int(src, "mic_sample_rate", s_cfg.mic_sample_rate, 8000, 48000);
  copy_int(src, "mic_seconds", s_cfg.mic_seconds, 1, 20);
  copy_int(src, "mic_gain", s_cfg.mic_gain, 25, 400);

  copy_int(src, "brightness", s_cfg.brightness, 5, 255);
  copy_int(src, "rotation", s_cfg.rotation, 0, 3);
  copy_bool(src, "face_enabled", s_cfg.face_enabled);

  sanitize();
  cfg_unlock();

  if (persist) return config_save(err);
  return true;
}

bool config_set_wifi(const String &ssid, const String &password, String &err) {
  cfg_lock();
  s_cfg.wifi_ssid     = ssid;
  s_cfg.wifi_password = password;
  cfg_unlock();
  return config_save(err);
}

bool config_begin() {
  if (!s_mutex) s_mutex = xSemaphoreCreateRecursiveMutex();

  cfg_lock();
  config_defaults(s_cfg);
  cfg_unlock();

  int result = 1;
  if (sd_ready()) {
    result = load_from_fs(SD, CONFIG_FILE_SD);
  }
  if (result == 1) {
    result = load_from_fs(SPIFFS, CONFIG_FILE_SPIFFS);
  }

  if (result == 1) {
    Serial.println("[cfg] no config.json found, creating defaults");
    String err;
    if (!config_save(err)) Serial.printf("[cfg] %s\n", err.c_str());
  } else if (result < 0) {
    Serial.println("[cfg] keeping built-in defaults (existing file left untouched)");
  }
  return true;
}
