#include "gemini.h"

#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "app_config.h"
#include "util.h"
#include "wifi_manager.h"

static const char *kAudioInstruction =
    "請先仔細聽這段錄音，辨識出使用者說的內容（可能是台灣中文或英文），"
    "然後用繁體中文簡短回答。只輸出下列 JSON 物件，不要加上任何其他文字、"
    "不要使用程式碼圍籬：\n"
    "{\"user\":\"辨識到的文字\",\"reply\":\"你的回答\"}";

bool geminiReady() {
  // 本機 / 自架的 OpenAI 相容服務可能不需要金鑰，因此只要求有端點。
  return !g_cfg.ai.api_url.isEmpty();
}

// ---------------------------------------------------------------- 端點設定

// ai.api_url 可自訂供應商，支援 {model} 與 {key} 兩個佔位符。
// ai.api_style 決定請求格式：
//   gemini = generateContent（預設）
//   openai = /v1/chat/completions（LM Studio、Ollama、one-api 等相容服務）
//   auto   = 依網址自動判斷
struct AiEndpoint {
  bool tls;
  bool openai;
  String host;
  uint16_t port;
  String path;
};

// OpenAI 相容服務的網址只要填到 .../v1，這裡自動補上 /chat/completions。
static String normalizeOpenAiPath(const String &in) {
  String path = in;
  String query;
  int q = path.indexOf('?');
  if (q >= 0) {
    query = path.substring(q);
    path = path.substring(0, q);
  }
  while (path.endsWith("/")) path.remove(path.length() - 1);
  if (!path.endsWith("/chat/completions")) {
    if (path.isEmpty()) path = "/v1";
    path += "/chat/completions";
  }
  return path + query;
}

static bool buildEndpoint(AiEndpoint &ep, String &err) {
  String url = g_cfg.ai.api_url;
  url.trim();
  if (url.isEmpty()) {
    err = "尚未設定 API 網址（config.json 的 ai.api_url）";
    return false;
  }
  url.replace("{model}", g_cfg.ai.model);
  url.replace("{key}", g_cfg.ai.api_key);
  url.replace("{MODEL}", g_cfg.ai.model);
  url.replace("{KEY}", g_cfg.ai.api_key);

  // 明確設定優先；auto 時再依網址判斷（預設 Gemini 端點含 :generateContent）。
  String style = g_cfg.ai.api_style;
  style.trim();
  style.toLowerCase();
  if (style != "gemini" && style != "openai") {
    if (url.indexOf(":generateContent") >= 0 ||
        url.indexOf("generativelanguage.googleapis.com") >= 0) {
      style = "gemini";
    } else if (url.indexOf("/chat/completions") >= 0 || url.endsWith("/v1") ||
               url.endsWith("/v1/") || url.indexOf("/v1/") >= 0) {
      style = "openai";
    } else {
      style = "gemini";
    }
  }
  ep.openai = (style == "openai");

  int sep = url.indexOf("://");
  if (sep <= 0) {
    err = "ai.api_url 必須以 http:// 或 https:// 開頭";
    return false;
  }
  String scheme = url.substring(0, sep);
  scheme.toLowerCase();
  if (scheme == "https") {
    ep.tls = true;
    ep.port = 443;
  } else if (scheme == "http") {
    ep.tls = false;
    ep.port = 80;
  } else {
    err = "ai.api_url 只支援 http:// 或 https://";
    return false;
  }

  String rest = url.substring(sep + 3);
  int cut = rest.indexOf('/');
  int q = rest.indexOf('?');
  if (q >= 0 && (cut < 0 || q < cut)) cut = q;
  String hostPort = cut < 0 ? rest : rest.substring(0, cut);
  if (cut < 0) {
    ep.path = "/";
  } else if (rest[cut] == '?') {
    ep.path = "/" + rest.substring(cut);
  } else {
    ep.path = rest.substring(cut);
  }

  int colon = hostPort.lastIndexOf(':');
  if (colon > 0) {
    long pv = hostPort.substring(colon + 1).toInt();
    if (pv > 0 && pv < 65536) {
      ep.port = (uint16_t)pv;
      hostPort = hostPort.substring(0, colon);
    }
  }
  ep.host = hostPort;
  if (ep.host.isEmpty()) {
    err = "ai.api_url 缺少主機名稱";
    return false;
  }
  if (ep.openai) ep.path = normalizeOpenAiPath(ep.path);
  return true;
}

// ---------------------------------------------------------------- HTTP

template <typename C>
static int readByte(C &c, uint32_t deadline) {
  while ((int32_t)(millis() - deadline) < 0) {
    int v = c.read();
    if (v >= 0) return v;
    if (!c.connected() && !c.available()) return -1;
    delay(2);
  }
  return -1;
}

template <typename C>
static String readLine(C &c, uint32_t deadline) {
  String line;
  while (line.length() < 512) {
    int v = readByte(c, deadline);
    if (v < 0) break;
    if (v == '\n') break;
    if (v != '\r') line += (char)v;
  }
  return line;
}

template <typename C>
static bool writeAll(C &c, const uint8_t *data, size_t len) {
  size_t sent = 0;
  uint32_t deadline = millis() + 20000;
  while (sent < len) {
    size_t n = c.write(data + sent, len - sent);
    if (n == 0) {
      if ((int32_t)(millis() - deadline) >= 0) return false;
      delay(5);
      continue;
    }
    sent += n;
  }
  return true;
}

template <typename C>
static bool httpExchange(C &client, const String &head, const uint8_t *body, size_t bodyLen,
                         String &resp, int &status, String &err) {
  if (!writeAll(client, (const uint8_t *)head.c_str(), head.length())) {
    err = "請求送出失敗（標頭）";
    client.stop();
    return false;
  }
  if (!writeAll(client, body, bodyLen)) {
    err = "請求送出失敗（內容）";
    client.stop();
    return false;
  }

  uint32_t deadline = millis() + g_cfg.ai.timeout_ms + 15000;

  String statusLine = readLine(client, deadline);
  status = 0;
  int sp = statusLine.indexOf(' ');
  if (sp > 0) status = statusLine.substring(sp + 1).toInt();
  if (status == 0) {
    err = "伺服器沒有回應";
    client.stop();
    return false;
  }

  bool chunked = false;
  long contentLength = -1;
  while (true) {
    String line = readLine(client, deadline);
    if (line.isEmpty()) break;
    String lower = line;
    lower.toLowerCase();
    if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) chunked = true;
    if (lower.startsWith("content-length:")) contentLength = lower.substring(15).toInt();
  }

  resp = "";
  resp.reserve(2048);
  const size_t kMaxResp = 24576;

  if (chunked) {
    while (resp.length() < kMaxResp) {
      String sizeLine = readLine(client, deadline);
      if (sizeLine.isEmpty()) {
        if (!client.connected()) break;
        continue;
      }
      long sz = strtol(sizeLine.c_str(), nullptr, 16);
      if (sz <= 0) break;
      for (long i = 0; i < sz && resp.length() < kMaxResp; i++) {
        int v = readByte(client, deadline);
        if (v < 0) break;
        resp += (char)v;
      }
      readLine(client, deadline); // 結尾 CRLF
    }
  } else if (contentLength > 0) {
    for (long i = 0; i < contentLength && resp.length() < kMaxResp; i++) {
      int v = readByte(client, deadline);
      if (v < 0) break;
      resp += (char)v;
    }
  } else {
    while (resp.length() < kMaxResp) {
      int v = readByte(client, deadline);
      if (v < 0) break;
      resp += (char)v;
    }
  }

  client.stop();
  return true;
}

static bool httpPost(const AiEndpoint &ep, const String &contentType, const uint8_t *body,
                     size_t bodyLen, String &resp, int &status, String &err) {
  String head;
  head.reserve(380);
  head += "POST ";
  head += ep.path;
  head += " HTTP/1.1\r\nHost: ";
  head += ep.host;
  head += "\r\nUser-Agent: TTGO-AI-Camera/1.0\r\nContent-Type: ";
  head += contentType;
  head += "\r\nAccept: application/json\r\n";
  if (ep.openai && !g_cfg.ai.api_key.isEmpty()) {
    head += "Authorization: Bearer ";
    head += g_cfg.ai.api_key;
    head += "\r\n";
  }
  head += "Content-Length: ";
  head += String((unsigned)bodyLen);
  head += "\r\nConnection: close\r\n\r\n";

  if (ep.tls) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(20);
    if (!client.connect(ep.host.c_str(), ep.port, 20000)) {
      err = String("無法連線到 ") + ep.host;
      return false;
    }
    return httpExchange(client, head, body, bodyLen, resp, status, err);
  }

  WiFiClient client;
  if (!client.connect(ep.host.c_str(), ep.port, 20000)) {
    err = String("無法連線到 ") + ep.host;
    return false;
  }
  return httpExchange(client, head, body, bodyLen, resp, status, err);
}

static bool httpPostJson(const AiEndpoint &ep, const uint8_t *body, size_t bodyLen, String &resp,
                         int &status, String &err) {
  return httpPost(ep, "application/json", body, bodyLen, resp, status, err);
}

// ---------------------------------------------------------------- 回應解析

static String stripCodeFence(const String &s) {
  String t = trimCopy(s);
  if (t.startsWith("```")) {
    int nl = t.indexOf('\n');
    if (nl >= 0) t = t.substring(nl + 1);
    int end = t.lastIndexOf("```");
    if (end >= 0) t = t.substring(0, end);
    t = trimCopy(t);
  }
  return t;
}

// 從模型輸出擷取 JSON 物件；找不到就整段當成回覆
static void parseModelText(const String &raw, AiReply &out) {
  String text = stripCodeFence(raw);
  int first = text.indexOf('{');
  int last = text.lastIndexOf('}');
  if (first >= 0 && last > first) {
    String candidate = text.substring(first, last + 1);
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, candidate);
    if (!e) {
      const char *reply = doc["reply"] | (const char *)nullptr;
      const char *user = doc["user"] | (const char *)nullptr;
      if (reply && *reply) {
        out.reply = reply;
        if (user) out.user = user;
        out.ok = true;
        return;
      }
    }
  }
  out.reply = text;
  out.ok = !text.isEmpty();
}

static bool parseApiResponse(const AiEndpoint &ep, const String &resp, int status,
                             AiReply &out) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, resp);
  if (e) {
    out.error = String("回應解析失敗 (HTTP ") + status + ")";
    return false;
  }

  if (status != 200) {
    const char *msg = doc["error"]["message"] | (const char *)nullptr;
    out.error = String("HTTP ") + status;
    if (msg) out.error += String("：") + msg;
    if (status == 404 && !ep.openai) {
      out.error +=
          "（若使用 OpenAI 相容服務，請把 ai.api_style 設為 openai，"
          "或把網址填到 .../v1/chat/completions）";
    }
    return false;
  }

  // OpenAI 相容格式：choices[0].message.content
  JsonArrayConst choices = doc["choices"];
  if (!choices.isNull() && choices.size() > 0) {
    const char *m = choices[0]["message"]["content"] | (const char *)nullptr;
    String text = m ? m : String();
    if (text.isEmpty()) {
      const char *t = choices[0]["text"] | (const char *)nullptr;
      if (t) text = t;
    }
    if (text.isEmpty()) {
      out.error = "模型回覆為空";
      return false;
    }
    parseModelText(text, out);
    return out.ok;
  }

  const char *block = doc["promptFeedback"]["blockReason"] | (const char *)nullptr;
  if (block) {
    out.error = String("內容被安全政策阻擋：") + block;
    return false;
  }

  JsonArrayConst candidates = doc["candidates"];
  if (candidates.isNull() || candidates.size() == 0) {
    out.error = "模型沒有回覆";
    return false;
  }

  String text;
  JsonArrayConst parts = candidates[0]["content"]["parts"];
  for (JsonVariantConst p : parts) {
    const char *t = p["text"] | (const char *)nullptr;
    if (t) text += t;
  }
  if (text.isEmpty()) {
    out.error = "模型回覆為空";
    return false;
  }
  parseModelText(text, out);
  return out.ok;
}

static void appendGenerationConfig(String &s) {
  s += ",\"generationConfig\":{\"temperature\":";
  s += String(g_cfg.ai.temperature, 2);
  s += ",\"maxOutputTokens\":";
  s += String(g_cfg.ai.max_output_tokens);
  s += "}";
}

// ---------------------------------------------------------------- OpenAI 相容語音

static const char *kFollowUpInstruction =
    "以下是把使用者的語音轉成的文字。請用繁體中文簡短回答，"
    "不要提到這段指示，也不要輸出 JSON 或程式碼圍籬。";

static bool openAiTranscribe(const AiEndpoint &chatEp, const uint8_t *wav, size_t wavLen,
                             String &text, String &err) {
  AiEndpoint tp = chatEp;
  int idx = tp.path.indexOf("/chat/completions");
  if (idx < 0) {
    err = "OpenAI 相容模式的語音需要 .../v1/chat/completions 形式的網址";
    return false;
  }
  tp.path = tp.path.substring(0, idx) + "/audio/transcriptions";

  const String boundary = "----TTGOAudio7f3c";
  String head;
  head.reserve(256);
  head += "--";
  head += boundary;
  head += "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
  head += "Content-Type: audio/wav\r\n\r\n";

  String tail;
  tail.reserve(320);
  tail += "\r\n--";
  tail += boundary;
  tail += "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n";
  tail += g_cfg.ai.stt_model;
  tail += "\r\n--";
  tail += boundary;
  tail += "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nzh\r\n";
  tail += "--";
  tail += boundary;
  tail += "--\r\n";

  size_t total = head.length() + wavLen + tail.length();
  uint8_t *body = (uint8_t *)ps_malloc(total);
  if (!body) body = (uint8_t *)malloc(total);
  if (!body) {
    err = "記憶體不足，無法組裝語音請求";
    return false;
  }
  memcpy(body, head.c_str(), head.length());
  memcpy(body + head.length(), wav, wavLen);
  memcpy(body + head.length() + wavLen, tail.c_str(), tail.length());

  String resp;
  int status = 0;
  String ctype = String("multipart/form-data; boundary=") + boundary;
  bool sent = httpPost(tp, ctype, body, total, resp, status, err);
  free(body);
  if (!sent) return false;

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, resp);
  if (e) {
    err = String("語音辨識回應解析失敗 (HTTP ") + status + ")";
    return false;
  }
  if (status != 200) {
    const char *msg = doc["error"]["message"] | (const char *)nullptr;
    err = String("語音辨識 HTTP ") + status;
    if (msg) err += String("：") + msg;
    return false;
  }
  const char *t2 = doc["text"] | (const char *)nullptr;
  if (!t2 || !*t2) {
    err = "語音辨識結果為空";
    return false;
  }
  text = t2;
  return true;
}

static bool openAiVoiceChat(const AiEndpoint &chatEp, const uint8_t *wav, size_t wavLen,
                            AiReply &out) {
  String text, err;
  if (!openAiTranscribe(chatEp, wav, wavLen, text, err)) {
    out.error = err;
    return false;
  }
  String prompt = String(kFollowUpInstruction) + "\n使用者說：" + text;
  if (!geminiChatText(prompt, out)) return false;
  out.user = text;
  return true;
}

// ---------------------------------------------------------------- 公開 API

bool geminiChatText(const String &text, AiReply &out) {
  out = AiReply();
  if (!geminiReady()) {
    out.error = "尚未設定 API 網址（config.json 的 ai.api_url）";
    return false;
  }
  if (!wifiConnected()) {
    out.error = "WiFi 未連線";
    return false;
  }

  AiEndpoint ep;
  String resp, err;
  int status = 0;
  if (!buildEndpoint(ep, err)) {
    out.error = err;
    return false;
  }

  String body;
  body.reserve(text.length() * 2 + 1024);
  if (ep.openai) {
    // OpenAI 相容：model + messages
    body += "{\"model\":\"";
    body += jsonEscape(g_cfg.ai.model);
    body += "\",\"messages\":[{\"role\":\"system\",\"content\":\"";
    body += jsonEscape(g_cfg.ai.system_prompt);
    body += "\"},{\"role\":\"user\",\"content\":\"";
    body += jsonEscape(text);
    body += "\"}]";
    body += ",\"temperature\":";
    body += String(g_cfg.ai.temperature, 2);
    body += ",\"max_tokens\":";
    body += String(g_cfg.ai.max_output_tokens);
    body += ",\"stream\":false}";
  } else {
    body += "{\"systemInstruction\":{\"parts\":[{\"text\":\"";
    body += jsonEscape(g_cfg.ai.system_prompt);
    body += "\"}]},\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"";
    body += jsonEscape(text);
    body += "\"}]}]";
    appendGenerationConfig(body);
    body += "}";
  }

  if (!httpPostJson(ep, (const uint8_t *)body.c_str(), body.length(), resp, status, err)) {
    out.error = err;
    return false;
  }
  out.status = status;
  return parseApiResponse(ep, resp, status, out);
}

bool geminiChatAudio(const uint8_t *wav, size_t wavLen, AiReply &out) {
  out = AiReply();
  if (!geminiReady()) {
    out.error = "尚未設定 API 網址（config.json 的 ai.api_url）";
    return false;
  }
  if (!wifiConnected()) {
    out.error = "WiFi 未連線";
    return false;
  }

  AiEndpoint ep;
  String err;
  if (!buildEndpoint(ep, err)) {
    out.error = err;
    return false;
  }
  if (ep.openai) return openAiVoiceChat(ep, wav, wavLen, out);

  String prefix;
  prefix.reserve(1024);
  prefix += "{\"systemInstruction\":{\"parts\":[{\"text\":\"";
  prefix += jsonEscape(g_cfg.ai.system_prompt);
  prefix += "\"}]},\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"";
  prefix += jsonEscape(kAudioInstruction);
  prefix += "\"},{\"inlineData\":{\"mimeType\":\"audio/wav\",\"data\":\"";

  String suffix;
  suffix.reserve(256);
  suffix += "\"}}]}]";
  appendGenerationConfig(suffix);
  suffix += "}";

  size_t b64Len = base64EncodedLength(wavLen);
  size_t total = prefix.length() + b64Len + suffix.length() + 1;

  uint8_t *body = (uint8_t *)ps_malloc(total);
  if (!body) body = (uint8_t *)malloc(total);
  if (!body) {
    out.error = "記憶體不足，無法組裝語音請求";
    return false;
  }

  memcpy(body, prefix.c_str(), prefix.length());
  base64Encode(wav, wavLen, (char *)(body + prefix.length()));
  memcpy(body + prefix.length() + b64Len, suffix.c_str(), suffix.length());
  size_t bodyLen = prefix.length() + b64Len + suffix.length();

  String resp;
  int status = 0;
  bool sent = httpPostJson(ep, body, bodyLen, resp, status, err);
  free(body);

  if (!sent) {
    out.error = err;
    return false;
  }
  out.status = status;
  return parseApiResponse(ep, resp, status, out);
}
