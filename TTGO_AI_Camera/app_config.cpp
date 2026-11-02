#include "app_config.h"

#include <ArduinoJson.h>

#include "storage.h"
#include "util.h"

AppConfig g_cfg;

static const char *kDefaultPrompt =
    "你是一個親切的繁體中文語音助理，名字叫「小 T」。"
    "請一律使用台灣繁體中文（正體中文）回答，語氣自然、簡潔、有禮貌。"
    "每次回覆不超過 120 個字，不要使用簡體字，也不要使用表情符號。";

void AppConfig::resetDefaults() {
  wifi = WifiCfg();
  ai = AiCfg();
  ai.system_prompt = kDefaultPrompt;
  audio = AudioCfg();
  bme = BmeCfg();
  camera = CameraCfg();
  display = DisplayCfg();
  web = WebCfg();
  font_path = "/font.bin";
  faces_path = "/faces.csv";
}

static void readString(JsonVariantConst v, String &dst) {
  if (!v.isNull()) dst = v.as<String>();
}

static void readU32(JsonVariantConst v, uint32_t &dst) {
  if (!v.isNull()) dst = v.as<uint32_t>();
}

static void readInt(JsonVariantConst v, int &dst) {
  if (!v.isNull()) dst = v.as<int>();
}

static void readBool(JsonVariantConst v, bool &dst) {
  if (!v.isNull()) dst = v.as<bool>();
}

static void readFloat(JsonVariantConst v, float &dst) {
  if (!v.isNull()) dst = v.as<float>();
}

bool AppConfig::fromJson(const String &json, String *err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) {
    if (err) *err = String("JSON 解析失敗: ") + e.c_str();
    return false;
  }
  if (!doc.is<JsonObjectConst>()) {
    if (err) *err = "JSON 根節點必須是物件";
    return false;
  }

  JsonObjectConst w = doc["wifi"];
  if (!w.isNull()) {
    readString(w["ssid"], wifi.ssid);
    readString(w["password"], wifi.password);
    readU32(w["connect_timeout_ms"], wifi.connect_timeout_ms);
    readString(w["ap_ssid"], wifi.ap_ssid);
    readString(w["ap_password"], wifi.ap_password);
  }

  JsonObjectConst a = doc["ai"];
  if (!a.isNull()) {
    String key;
    readString(a["api_key"], key);
    // 網頁介面以 *** 代表「不變更」
    if (key != "***") ai.api_key = key;
    readString(a["model"], ai.model);
    readString(a["system_prompt"], ai.system_prompt);
    readU32(a["timeout_ms"], ai.timeout_ms);
    readInt(a["max_output_tokens"], ai.max_output_tokens);
    readFloat(a["temperature"], ai.temperature);
  }

  JsonObjectConst au = doc["audio"];
  if (!au.isNull()) {
    readU32(au["sample_rate"], audio.sample_rate);
    readU32(au["max_record_ms"], audio.max_record_ms);
    readU32(au["min_record_ms"], audio.min_record_ms);
    readU32(au["silence_ms"], audio.silence_ms);
    readInt(au["vad_threshold"], audio.vad_threshold);
    readInt(au["gain"], audio.gain);
    readBool(au["enabled"], audio.enabled);
  }

  JsonObjectConst b = doc["bme280"];
  if (!b.isNull()) {
    readBool(b["enabled"], bme.enabled);
    int addr = bme.address;
    readInt(b["address"], addr);
    bme.address = (uint8_t)addr;
    readU32(b["interval_ms"], bme.interval_ms);
  }

  JsonObjectConst c = doc["camera"];
  if (!c.isNull()) {
    readInt(c["xclk_hz"], camera.xclk_hz);
    readInt(c["framesize"], camera.framesize);
    readInt(c["jpeg_quality"], camera.jpeg_quality);
    readFloat(c["rec_threshold"], camera.rec_threshold);
    readInt(c["enroll_samples"], camera.enroll_samples);
    readInt(c["max_faces"], camera.max_faces);
    readInt(c["vflip"], camera.vflip);
    readInt(c["hmirror"], camera.hmirror);
  }

  JsonObjectConst d = doc["display"];
  if (!d.isNull()) {
    readInt(d["brightness"], display.brightness);
    readU32(d["nav_timeout_ms"], display.nav_timeout_ms);
    readBool(d["auto_run"], display.auto_run);
  }

  JsonObjectConst wb = doc["web"];
  if (!wb.isNull()) {
    readBool(wb["enabled"], web.enabled);
    readString(wb["user"], web.user);
    readString(wb["password"], web.password);
  }

  readString(doc["font_path"], font_path);
  readString(doc["faces_path"], faces_path);

  // 邊界檢查
  if (audio.sample_rate < 8000 || audio.sample_rate > 48000) audio.sample_rate = 16000;
  if (audio.gain < 1) audio.gain = 1;
  if (audio.gain > 8) audio.gain = 8;
  if (display.brightness < 0) display.brightness = 0;
  if (display.brightness > 255) display.brightness = 255;
  if (display.nav_timeout_ms < 800) display.nav_timeout_ms = 800;
  if (display.nav_timeout_ms > 20000) display.nav_timeout_ms = 20000;
  if (camera.enroll_samples < 1) camera.enroll_samples = 1;
  if (camera.enroll_samples > 10) camera.enroll_samples = 10;
  if (camera.max_faces < 1) camera.max_faces = 1;
  if (camera.max_faces > 16) camera.max_faces = 16;
  if (camera.rec_threshold < 0.1f) camera.rec_threshold = 0.1f;
  if (camera.rec_threshold > 0.99f) camera.rec_threshold = 0.99f;
  if (bme.interval_ms < 500) bme.interval_ms = 500;
  if (bme.interval_ms > 60000) bme.interval_ms = 60000;
  if (ai.max_output_tokens < 32) ai.max_output_tokens = 32;
  if (ai.max_output_tokens > 2048) ai.max_output_tokens = 2048;
  if (ai.system_prompt.isEmpty()) ai.system_prompt = kDefaultPrompt;
  if (font_path.isEmpty()) font_path = "/font.bin";
  if (faces_path.isEmpty()) faces_path = "/faces.csv";
  return true;
}

String AppConfig::toJson(bool maskSecrets) const {
  JsonDocument doc;
  JsonObject w = doc["wifi"].to<JsonObject>();
  w["ssid"] = wifi.ssid;
  w["password"] = maskSecrets && !wifi.password.isEmpty() ? "***" : wifi.password;
  w["connect_timeout_ms"] = wifi.connect_timeout_ms;
  w["ap_ssid"] = wifi.ap_ssid;
  w["ap_password"] = wifi.ap_password;

  JsonObject a = doc["ai"].to<JsonObject>();
  a["api_key"] = maskSecrets && !ai.api_key.isEmpty() ? "***" : ai.api_key;
  a["model"] = ai.model;
  a["system_prompt"] = ai.system_prompt;
  a["timeout_ms"] = ai.timeout_ms;
  a["max_output_tokens"] = ai.max_output_tokens;
  a["temperature"] = ai.temperature;

  JsonObject au = doc["audio"].to<JsonObject>();
  au["sample_rate"] = audio.sample_rate;
  au["max_record_ms"] = audio.max_record_ms;
  au["min_record_ms"] = audio.min_record_ms;
  au["silence_ms"] = audio.silence_ms;
  au["vad_threshold"] = audio.vad_threshold;
  au["gain"] = audio.gain;
  au["enabled"] = audio.enabled;

  JsonObject b = doc["bme280"].to<JsonObject>();
  b["enabled"] = bme.enabled;
  b["address"] = bme.address;
  b["interval_ms"] = bme.interval_ms;

  JsonObject c = doc["camera"].to<JsonObject>();
  c["xclk_hz"] = camera.xclk_hz;
  c["framesize"] = camera.framesize;
  c["jpeg_quality"] = camera.jpeg_quality;
  c["rec_threshold"] = camera.rec_threshold;
  c["enroll_samples"] = camera.enroll_samples;
  c["max_faces"] = camera.max_faces;
  c["vflip"] = camera.vflip;
  c["hmirror"] = camera.hmirror;

  JsonObject d = doc["display"].to<JsonObject>();
  d["brightness"] = display.brightness;
  d["nav_timeout_ms"] = display.nav_timeout_ms;
  d["auto_run"] = display.auto_run;

  JsonObject wb = doc["web"].to<JsonObject>();
  wb["enabled"] = web.enabled;
  wb["user"] = web.user;
  wb["password"] = maskSecrets && !web.password.isEmpty() ? "***" : web.password;

  doc["font_path"] = font_path;
  doc["faces_path"] = faces_path;

  String out;
  out.reserve(1536);
  serializeJsonPretty(doc, out);
  return out;
}

bool AppConfig::load() {
  resetDefaults();
  if (!storageReady()) {
    logLine("CFG", "SD 卡未就緒，使用預設設定");
    return false;
  }
  if (!storageExists(APP_CONFIG_PATH)) {
    logLine("CFG", "找不到 %s，建立預設設定檔", APP_CONFIG_PATH);
    save();
    return true;
  }
  String raw = storageRead(APP_CONFIG_PATH);
  if (raw.isEmpty()) {
    logLine("CFG", "設定檔為空");
    return false;
  }
  String err;
  if (!fromJson(raw, &err)) {
    logLine("CFG", "%s", err.c_str());
    return false;
  }
  logLine("CFG", "設定已載入（模型：%s）", ai.model.c_str());
  return true;
}

bool AppConfig::save() {
  if (!storageReady()) return false;
  String json = toJson(false);
  bool ok = storageWrite(APP_CONFIG_PATH, json);
  logLine("CFG", ok ? "設定已儲存" : "設定儲存失敗");
  return ok;
}
