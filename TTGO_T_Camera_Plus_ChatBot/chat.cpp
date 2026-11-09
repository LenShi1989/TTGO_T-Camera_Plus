// ============================================================================
//  chat.cpp - OpenAI compatible / Ollama chat + OpenAI compatible Whisper STT
// ============================================================================
#include "chat.h"

#include <FS.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <vector>

#include "app_config.h"
#include "board_config.h"
#include "storage.h"

struct ChatMessage {
  String role;
  String content;
};

static std::vector<ChatMessage> s_history;
static SemaphoreHandle_t        s_mutex = nullptr;

static const size_t kMaxStoredChars = 900;

// ---------------------------------------------------------------------------
//  history
// ---------------------------------------------------------------------------
void chat_begin() {
  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
}

static void trim_history(int max_exchanges) {
  if (max_exchanges <= 0) {
    s_history.clear();
    return;
  }
  size_t max_msgs = (size_t)max_exchanges * 2;
  while (s_history.size() > max_msgs) {
    s_history.erase(s_history.begin());
  }
  // A request must never start with an assistant message: providers reject it.
  while (!s_history.empty() && s_history.front().role != "user") {
    s_history.erase(s_history.begin());
  }
}

void chat_reset() {
  if (!s_mutex) return;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_history.clear();
  xSemaphoreGive(s_mutex);
}

int chat_history_count() {
  if (!s_mutex) return 0;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  int n = (int)s_history.size();
  xSemaphoreGive(s_mutex);
  return n;
}

String chat_history_json() {
  String out = "{\"ok\":true,\"messages\":[";
  if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
  for (size_t i = 0; i < s_history.size(); ++i) {
    if (i) out += ',';
    out += "{\"role\":\"";
    out += json_escape(s_history[i].role);
    out += "\",\"content\":\"";
    out += json_escape(s_history[i].content);
    out += "\"}";
  }
  if (s_mutex) xSemaphoreGive(s_mutex);
  out += "]}";
  return out;
}

// ---------------------------------------------------------------------------
//  url helpers
// ---------------------------------------------------------------------------
static String strip_trailing_slash(String s) {
  s.trim();
  while (s.endsWith("/")) s.remove(s.length() - 1);
  return s;
}

// Users often type "192.168.1.10:8000" without a scheme.  HTTPClient::begin()
// rejects such a URL, which surfaced as "無法建立 HTTP 連線", so default to
// plain HTTP when no scheme was given.
static String ensure_scheme(String s) {
  s.trim();
  if (s.length() == 0) return s;
  if (s.indexOf("://") < 0) s = "http://" + s;
  return s;
}

static String join_endpoint(const String &base, const String &suffix) {
  String b = ensure_scheme(strip_trailing_slash(base));
  if (b.endsWith(suffix)) return b;
  return b + suffix;
}

static String stt_endpoint(const AppConfig &cfg) {
  String base = ensure_scheme(cfg.stt_base_url.length() ? cfg.stt_base_url : cfg.ai_base_url);
  base = strip_trailing_slash(base);
  const String suffix = "/audio/transcriptions";
  if (base.endsWith(suffix)) return base;
  if (base.endsWith("/chat/completions")) {
    base = base.substring(0, base.length() - 17);
  }
  return base + suffix;
}

static String voice_endpoint(const AppConfig &cfg) {
  String base = ensure_scheme(cfg.stt_base_url.length() ? cfg.stt_base_url : cfg.ai_base_url);
  base = strip_trailing_slash(base);
  if (base.endsWith("/audio/transcriptions")) {
    base = base.substring(0, base.length() - 21);
  } else if (base.endsWith("/chat/completions")) {
    base = base.substring(0, base.length() - 17);
  }
  return base + "/voice/chat";
}

// ---------------------------------------------------------------------------
//  tiny HTTP client
// ---------------------------------------------------------------------------
static bool http_post(const AppConfig &cfg, const String &url, const String &api_key,
                      const String &content_type, const uint8_t *body, size_t body_len,
                      String &response, int &code, String &err) {
  WiFiClient plain;
  WiFiClientSecure tls;
  HTTPClient http;

  // HTTPClient stores the timeout in a uint16_t (max 65535 ms).
  uint32_t timeout_ms = (uint32_t)cfg.ai_timeout_ms;
  if (timeout_ms > 60000) timeout_ms = 60000;
  if (timeout_ms < 5000)  timeout_ms = 5000;
  http.setTimeout((uint16_t)timeout_ms);
  http.setConnectTimeout(10000);
  http.setReuse(false);

  // Last line of defence: never hand HTTPClient a URL without http(s)://.
  String target = ensure_scheme(url);
  bool started = false;
  if (target.startsWith("https://")) {
    tls.setInsecure();
    tls.setHandshakeTimeout(20);
    started = http.begin(tls, target);
  } else if (target.startsWith("http://")) {
    started = http.begin(plain, target);
  }
  if (!started) {
    err = "無法建立 HTTP 連線";
    return false;
  }

  http.addHeader("Content-Type", content_type);
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  http.setUserAgent("TTGO-TCamera-Plus-ChatBot/" APP_VERSION);
  if (api_key.length()) http.addHeader("Authorization", "Bearer " + api_key);

  code = http.POST((uint8_t *)body, body_len);
  if (code <= 0) {
    err = String("網路錯誤：") + http.errorToString(code);
    http.end();
    return false;
  }

  response = http.getString();
  http.end();

  if (response.length() == 0) {
    err = String("伺服器回應為空（HTTP ") + code + "）";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
//  response parsing
// ---------------------------------------------------------------------------
static bool parse_chat_body(const String &body, String &out, String &err) {
  DynamicJsonDocument doc(20480);
  DeserializationError e = deserializeJson(doc, body);
  if (e) {
    err = String("無法解析 AI 回應：") + e.c_str();
    return false;
  }

  if (doc["error"].is<JsonObjectConst>()) {
    const char *m = doc["error"]["message"] | "";
    err = String("AI 服務錯誤：") + m;
    return false;
  }
  if (doc["error"].is<const char *>()) {
    err = String("AI 服務錯誤：") + (const char *)doc["error"];
    return false;
  }

  const char *content = nullptr;
  if (doc["choices"].is<JsonArrayConst>() && doc["choices"].size() > 0) {
    JsonVariantConst v = doc["choices"][0]["message"]["content"];
    if (v.is<const char *>()) content = v.as<const char *>();
  }
  if (!content && doc["message"].is<JsonObjectConst>()) {
    JsonVariantConst v = doc["message"]["content"];
    if (v.is<const char *>()) content = v.as<const char *>();
  }
  if (!content || !*content) {
    const char *thinking = doc["message"]["thinking"] | (const char *)nullptr;
    err = (thinking && *thinking)
              ? "模型只回傳了思考過程、沒有正文：請提高 max tokens，或確認 Ollama 支援並接受 think:false"
              : "AI 回應中找不到內容";
    return false;
  }
  out = content;
  return true;
}

static String tidy_reply(String s) {
  s.replace("\r", "");
  s.trim();
  while (s.indexOf("\n\n\n") >= 0) s.replace("\n\n\n", "\n\n");
  return s;
}

// ---------------------------------------------------------------------------
//  chat
// ---------------------------------------------------------------------------
bool chat_perform(const String &user_text, String &reply, String &err,
                  ChatProgressFn progress) {
  err = "";
  reply = "";

  if (!s_mutex) chat_begin();
  AppConfig cfg;
  config_snapshot(cfg);

  if (user_text.length() == 0) {
    err = "訊息是空的";
    return false;
  }
  if (cfg.ai_base_url.length() == 0) {
    err = "尚未設定 AI 服務網址";
    return false;
  }
  if (cfg.ai_mode != "ollama" && cfg.ai_api_key.length() == 0) {
    err = "尚未設定 API Key（使用本地 Ollama 請把模式改為 ollama）";
    return false;
  }

  if (progress) progress(8, "準備訊息");

  DynamicJsonDocument req(20480);
  req["model"] = cfg.ai_model;
  req["stream"] = false;
  if (cfg.ai_mode == "ollama") {
    // Thinking models (qwen3 / qwen3.5 ...) spend the whole num_predict budget on
    // their reasoning trace and return an empty message.content, which looks like
    // a failure.  The 240x240 reply box has no room for a reasoning dump anyway.
    req["think"] = false;
    JsonObject options = req.createNestedObject("options");
    options["temperature"] = cfg.ai_temperature;
    options["num_predict"] = cfg.ai_max_tokens;
  } else {
    req["temperature"] = cfg.ai_temperature;
    req["max_tokens"] = cfg.ai_max_tokens;
  }

  JsonArray msgs = req.createNestedArray("messages");
  JsonObject sys = msgs.createNestedObject();
  sys["role"] = "system";
  sys["content"] = cfg.ai_system_prompt;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  // Previous turns first; the new user message is always appended separately so
  // that ai_history == 0 still sends the current message to the provider.
  for (size_t i = 0; i < s_history.size(); ++i) {
    JsonObject o = msgs.createNestedObject();
    o["role"] = s_history[i].role;
    o["content"] = s_history[i].content;
  }
  ChatMessage um;
  um.role = "user";
  um.content = user_text.length() > kMaxStoredChars ? user_text.substring(0, kMaxStoredChars)
                                                    : user_text;
  JsonObject uo = msgs.createNestedObject();
  uo["role"] = "user";
  uo["content"] = um.content;
  s_history.push_back(um);
  trim_history(cfg.ai_history);
  xSemaphoreGive(s_mutex);

  String payload;
  serializeJson(req, payload);

  String url = (cfg.ai_mode == "ollama")
                   ? join_endpoint(cfg.ai_base_url, "/api/chat")
                   : join_endpoint(cfg.ai_base_url, "/chat/completions");

  if (progress) progress(22, "連線 AI");

  String response;
  int code = 0;
  if (!http_post(cfg, url, cfg.ai_api_key, "application/json",
                 (const uint8_t *)payload.c_str(), payload.length(),
                 response, code, err)) {
    return false;
  }

  if (progress) progress(82, "整理回應");

  if (!parse_chat_body(response, reply, err)) {
    if (code >= 400) err = String("HTTP ") + code + " - " + err;
    return false;
  }

  reply = tidy_reply(reply);

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  ChatMessage am;
  am.role = "assistant";
  am.content = reply;
  s_history.push_back(am);
  trim_history(cfg.ai_history);
  xSemaphoreGive(s_mutex);

  if (progress) progress(95, "完成");
  return true;
}

// ---------------------------------------------------------------------------
//  speech to text (OpenAI compatible /audio/transcriptions)
// ---------------------------------------------------------------------------
bool chat_transcribe(const String &wav_path, String &text, String &err,
                     ChatProgressFn progress) {
  err = "";
  text = "";

  AppConfig cfg;
  config_snapshot(cfg);
  if (!cfg.stt_enabled) {
    err = "尚未啟用語音辨識（STT），請先在 AI 設定中開啟";
    return false;
  }

  String norm;
  if (!sd_norm(wav_path, norm)) {
    err = "錄音檔路徑不合法";
    return false;
  }

  if (progress) progress(5, "讀取音檔");

  size_t size = 0;
  uint8_t *wav = nullptr;
  if (!bus_lock(6000)) {
    err = "SD 卡忙碌中";
    return false;
  }
  File f = SD.open(norm, "r");
  if (f && !f.isDirectory()) {
    size = f.size();
    if (size > 0 && size < 8 * 1024 * 1024) {
      wav = (uint8_t *)ps_malloc(size);
      if (!wav) wav = (uint8_t *)malloc(size);
      if (wav) {
        size_t got = f.read(wav, size);
        if (got != size) {
          free(wav);
          wav = nullptr;
          err = "讀取錄音檔失敗";
        }
      } else {
        err = "記憶體不足，無法讀取錄音檔";
      }
    } else {
      err = "錄音檔大小異常";
    }
  } else {
    err = "找不到錄音檔";
  }
  f.close();
  bus_unlock();

  if (!wav) return false;

  String boundary = "----TTCam" + String((uint32_t)millis());
  String head = "--" + boundary +
                "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\n"
                "Content-Type: audio/wav\r\n\r\n";
  String mid = "\r\n--" + boundary +
               "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" +
               cfg.stt_model + "\r\n";
  if (cfg.stt_language.length()) {
    mid += "--" + boundary +
           "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n" +
           cfg.stt_language + "\r\n";
  }
  mid += "--" + boundary + "--\r\n";

  size_t total = head.length() + size + mid.length();
  uint8_t *body = (uint8_t *)ps_malloc(total);
  if (!body) body = (uint8_t *)malloc(total);
  if (!body) {
    free(wav);
    err = "記憶體不足，無法建立上傳內容";
    return false;
  }

  memcpy(body, head.c_str(), head.length());
  memcpy(body + head.length(), wav, size);
  memcpy(body + head.length() + size, mid.c_str(), mid.length());
  free(wav);

  if (progress) progress(35, "上傳語音");

  String url = stt_endpoint(cfg);
  String api_key = cfg.stt_api_key.length() ? cfg.stt_api_key : cfg.ai_api_key;

  String response;
  int code = 0;
  bool sent = http_post(cfg, url, api_key,
                        "multipart/form-data; boundary=" + boundary,
                        body, total, response, code, err);
  free(body);
  if (!sent) return false;

  if (progress) progress(80, "語音辨識");

  DynamicJsonDocument doc(6144);
  DeserializationError e = deserializeJson(doc, response);
  if (e) {
    err = String("無法解析語音辨識回應：") + e.c_str();
    if (code >= 400) err = String("HTTP ") + code + " - " + err;
    return false;
  }

  JsonVariantConst tv = doc["text"];
  if (!tv.is<const char *>() || !*tv.as<const char *>()) {
    if (doc["error"].is<JsonObjectConst>()) {
      err = String("語音辨識錯誤：") + (const char *)(doc["error"]["message"] | "");
    } else {
      err = "語音辨識沒有回傳文字";
      if (code >= 400) err = String("HTTP ") + code + " - " + err;
    }
    return false;
  }

  text = tv.as<const char *>();
  text.trim();
  if (text.length() == 0) {
    err = "沒有辨識到語音內容";
    return false;
  }
  if (progress) progress(90, "辨識完成");
  return true;
}

// ---------------------------------------------------------------------------
//  voice chat (一體化：STT + AI 回應)
// ---------------------------------------------------------------------------
bool chat_voice_chat(const String &wav_path, String &transcript, String &reply, String &err,
                     ChatProgressFn progress) {
  err = "";
  transcript = "";
  reply = "";

  AppConfig cfg;
  config_snapshot(cfg);
  if (!cfg.stt_enabled) {
    err = "尚未啟用語音辨識（STT），請先在 AI 設定中開啟";
    return false;
  }

  String norm;
  if (!sd_norm(wav_path, norm)) {
    err = "錄音檔路徑不合法";
    return false;
  }

  if (progress) progress(5, "讀取音檔");

  size_t size = 0;
  uint8_t *wav = nullptr;
  if (!bus_lock(6000)) {
    err = "SD 卡忙碌中";
    return false;
  }
  File f = SD.open(norm, "r");
  if (f && !f.isDirectory()) {
    size = f.size();
    if (size > 0 && size < 8 * 1024 * 1024) {
      wav = (uint8_t *)ps_malloc(size);
      if (!wav) wav = (uint8_t *)malloc(size);
      if (wav) {
        size_t got = f.read(wav, size);
        if (got != size) {
          free(wav);
          wav = nullptr;
          err = "讀取錄音檔失敗";
        }
      } else {
        err = "記憶體不足，無法讀取錄音檔";
      }
    } else {
      err = "錄音檔大小異常";
    }
  } else {
    err = "找不到錄音檔";
  }
  f.close();
  bus_unlock();

  if (!wav) return false;

  // 上傳到一體化端點 /voice/chat
  String boundary = "----TTCam" + String((uint32_t)millis());
  String head = "--" + boundary +
                "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\n"
                "Content-Type: audio/wav\r\n\r\n";
  String mid = "\r\n--" + boundary +
               "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n" +
               (cfg.stt_language.length() ? cfg.stt_language : "zh") + "\r\n" +
               "--" + boundary + "--\r\n";

  size_t total = head.length() + size + mid.length();
  uint8_t *body = (uint8_t *)ps_malloc(total);
  if (!body) body = (uint8_t *)malloc(total);
  if (!body) {
    free(wav);
    err = "記憶體不足，無法建立上傳內容";
    return false;
  }

  memcpy(body, head.c_str(), head.length());
  memcpy(body + head.length(), wav, size);
  memcpy(body + head.length() + size, mid.c_str(), mid.length());
  free(wav);

  if (progress) progress(35, "上傳語音");

  // 一體化端點 /voice/chat：從同一個 Base URL 組出來，不要切割完整網址
  // （舊寫法用 stt_endpoint 再減 22 字元，會把 :8000 的結尾切掉）。
  String url = voice_endpoint(cfg);

  String api_key = cfg.stt_api_key.length() ? cfg.stt_api_key : cfg.ai_api_key;

  String response;
  int code = 0;
  bool sent = http_post(cfg, url, api_key,
                        "multipart/form-data; boundary=" + boundary,
                        body, total, response, code, err);
  free(body);
  if (!sent) return false;

  if (progress) progress(80, "語音辨識 + AI 理解");

  DynamicJsonDocument doc(10240);
  DeserializationError e = deserializeJson(doc, response);
  if (e) {
    err = String("無法解析語音對話回應：") + e.c_str();
    if (code >= 400) err = String("HTTP ") + code + " - " + err;
    return false;
  }

  // 解析 transcript
  JsonVariantConst tv = doc["transcript"];
  if (tv.is<const char *>() && *tv.as<const char *>()) {
    transcript = tv.as<const char *>();
    transcript.trim();
  }

  // 解析 reply
  JsonVariantConst rv = doc["reply"];
  if (rv.is<const char *>() && *rv.as<const char *>()) {
    reply = rv.as<const char *>();
    reply.trim();
  }

  if (!reply.length()) {
    err = "AI 未回傳任何內容";
    if (code >= 400) err = String("HTTP ") + code + " - " + err;
    return false;
  }

  if (progress) progress(95, "完成");
  return true;
}
