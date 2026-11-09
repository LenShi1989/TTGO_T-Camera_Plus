// ============================================================================
//  web_ui.cpp - HTTP server
//
//  * /            and other static paths come from SPIFFS (data/index.html,
//                 data/app.js, data/style.css, data/font_tc16.bin)
//  * /api/*       JSON endpoints for the sidebar web UI
//
//  The WebServer is single threaded and runs from loop(); the blocking AI /
//  microphone work happens in the jobs.cpp worker task so the UI stays alive.
// ============================================================================
#include "web_ui.h"

#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include "app_config.h"
#include "app_wifi.h"
#include "board_config.h"
#include "chat.h"
#include "display.h"
#include "jobs.h"
#include "storage.h"

static WebServer s_server(80);

// --- upload state ----------------------------------------------------------
static File     s_upload_file;
static String   s_upload_dir;
static String   s_upload_path;
static String   s_upload_error;
static bool     s_upload_ok      = false;
static bool     s_upload_active  = false;
static uint64_t s_upload_bytes   = 0;
static uint64_t s_upload_declared = 0;

// --- deferred reboot -------------------------------------------------------
static bool     s_reboot_pending = false;
static uint32_t s_reboot_at      = 0;

// ============================================================================
//  small helpers
// ============================================================================
static void send_json(int code, const String &body) {
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(code, "application/json; charset=utf-8", body);
}

static void send_ok() {
  send_json(200, "{\"ok\":true}");
}

static void send_error(int code, const String &message) {
  send_json(code, String("{\"ok\":false,\"error\":\"") + json_escape(message) + "\"}");
}

static bool parse_body(DynamicJsonDocument &doc) {
  if (!s_server.hasArg("plain")) return false;
  if (s_server.arg("plain").length() == 0) return false;
  DeserializationError e = deserializeJson(doc, s_server.arg("plain"));
  return !e;
}

static String arg_or(const char *name, const char *fallback) {
  if (s_server.hasArg(name)) {
    String v = s_server.arg(name);
    if (v.length()) return v;
  }
  return String(fallback);
}

static const char *mime_for(const String &path) {
  int dot = path.lastIndexOf('.');
  String ext = (dot >= 0) ? path.substring(dot + 1) : String();
  ext.toLowerCase();
  if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
  if (ext == "css")  return "text/css; charset=utf-8";
  if (ext == "js")   return "application/javascript; charset=utf-8";
  if (ext == "json") return "application/json; charset=utf-8";
  if (ext == "txt" || ext == "md" || ext == "log" || ext == "csv") return "text/plain; charset=utf-8";
  if (ext == "png")  return "image/png";
  if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
  if (ext == "gif")  return "image/gif";
  if (ext == "webp") return "image/webp";
  if (ext == "svg")  return "image/svg+xml";
  if (ext == "ico")  return "image/x-icon";
  if (ext == "bmp")  return "image/bmp";
  if (ext == "wav")  return "audio/wav";
  if (ext == "mp3")  return "audio/mpeg";
  if (ext == "woff2") return "font/woff2";
  return "application/octet-stream";
}

// Percent-encode a value for Content-Disposition (UTF-8 safe).
static String rfc5987(const String &value) {
  static const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() * 3);
  for (size_t i = 0; i < value.length(); ++i) {
    uint8_t c = (uint8_t)value[i];
    bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

// ============================================================================
//  GET /api/status
// ============================================================================
static void api_status() {
  AppConfig cfg;
  config_snapshot(cfg);

  char iso[32] = "--";
  time_t now = time(nullptr);
  if (now > 1600000000) {
    struct tm tmv;
    if (localtime_r(&now, &tmv)) strftime(iso, sizeof(iso), "%Y-%m-%d %H:%M:%S", &tmv);
  }

  String out;
  out.reserve(1600);
  out  = "{\"ok\":true";
  out += ",\"name\":\"";      out += json_escape(cfg.device_name);
  out += "\",\"version\":\""; out += APP_VERSION;
  out += "\",\"chip\":\"";    out += ESP.getChipModel();
  out += "\",\"cores\":";     out += ESP.getChipCores();
  out += ",\"cpu_mhz\":";     out += ESP.getCpuFreqMHz();
  out += ",\"sdk\":\"";       out += json_escape(String(ESP.getSdkVersion()));
  out += "\",\"flash\":";     out += (unsigned long)ESP.getFlashChipSize();
  out += ",\"heap_free\":";   out += (unsigned long)ESP.getFreeHeap();
  out += ",\"heap_total\":";  out += (unsigned long)ESP.getHeapSize();
  out += ",\"heap_min\":";    out += (unsigned long)ESP.getMinFreeHeap();
  out += ",\"psram_total\":"; out += (unsigned long)ESP.getPsramSize();
  out += ",\"psram_free\":";  out += (unsigned long)ESP.getFreePsram();
  out += ",\"uptime\":";      out += (unsigned long)(millis() / 1000);
  out += ",\"time\":\"";      out += iso;
  out += "\",\"hostname\":\""; out += json_escape(wifi_hostname());
  out += "\",\"face\":\"";    out += display_face_name(display_current_face());
  out += "\",\"brightness\":"; out += cfg.brightness;
  out += ",\"rotation\":";    out += cfg.rotation;
  out += ",\"face_enabled\":"; out += cfg.face_enabled ? "true" : "false";
  out += ",\"chat_messages\":"; out += chat_history_count();

  out += ",\"wifi\":";
  out += wifi_status_json();

  out += ",\"sd\":{\"ready\":";
  if (sd_ready()) {
    out += "true,\"total\":"; out += u64_to_string(SD.totalBytes());
    out += ",\"used\":";      out += u64_to_string(SD.usedBytes());
  } else {
    out += "false,\"total\":0,\"used\":0";
  }
  out += "}";

  out += ",\"spiffs\":{\"ready\":";
  if (spiffs_ready()) {
    out += "true,\"total\":"; out += u64_to_string(SPIFFS.totalBytes());
    out += ",\"used\":";      out += u64_to_string(SPIFFS.usedBytes());
  } else {
    out += "false,\"total\":0,\"used\":0";
  }
  out += "}";

  out += ",\"job\":";
  out += jobs_status_json();
  out += "}";

  send_json(200, out);
}

// ============================================================================
//  SPIFFS listing
// ============================================================================
static void api_spiffs() {
  String out;
  if (!spiffs_list_json(out)) {
    send_error(500, "SPIFFS 未掛載");
    return;
  }
  send_json(200, out);
}

// ============================================================================
//  SD card: listing / mkdir / delete / rename / read / write / download
// ============================================================================
static void api_sd_list() {
  String path = arg_or("path", "/");
  String out, err;
  if (!sd_list_json(path, out, err)) {
    send_error(400, err);
    return;
  }
  send_json(200, out);
}

static void api_sd_mkdir() {
  DynamicJsonDocument doc(1024);
  if (!parse_body(doc)) {
    send_error(400, "JSON 格式錯誤");
    return;
  }
  String path = doc["path"] | "";
  String err;
  if (path.length() == 0 || !sd_ensure_dir(path, err)) {
    send_error(400, err.length() ? err : "缺少資料夾路徑");
    return;
  }
  send_ok();
}

static void api_sd_delete() {
  DynamicJsonDocument doc(1024);
  if (!parse_body(doc)) {
    send_error(400, "JSON 格式錯誤");
    return;
  }
  String path = doc["path"] | "";
  if (path.length() == 0) {
    send_error(400, "缺少路徑");
    return;
  }
  String err;
  if (!sd_remove(path, err)) {
    send_error(400, err);
    return;
  }
  send_ok();
}

static void api_sd_rename() {
  DynamicJsonDocument doc(1536);
  if (!parse_body(doc)) {
    send_error(400, "JSON 格式錯誤");
    return;
  }
  String from = doc["from"] | "";
  String to   = doc["to"] | "";
  if (from.length() == 0 || to.length() == 0) {
    send_error(400, "缺少來源或目標路徑");
    return;
  }
  String err;
  if (!sd_rename(from, to, err)) {
    send_error(400, err);
    return;
  }
  send_ok();
}

static void api_sd_read() {
  String path = arg_or("path", "");
  if (path.length() == 0) {
    send_error(400, "缺少路徑");
    return;
  }
  String norm;
  if (!sd_norm(path, norm)) {
    send_error(400, "路徑不合法");
    return;
  }

  String content, err;
  if (!sd_read_text(norm, SD_TEXT_MAX_BYTES, content, err)) {
    send_error(400, err);
    return;
  }

  String out;
  out.reserve(content.length() + 200);
  out  = "{\"ok\":true,\"path\":\""; out += json_escape(norm);
  out += "\",\"size\":";             out += (unsigned long)content.length();
  out += ",\"truncated\":";          out += (content.length() >= SD_TEXT_MAX_BYTES) ? "true" : "false";
  out += ",\"content\":\"";          out += json_escape(content);
  out += "\"}";
  send_json(200, out);
}

static void api_sd_write() {
  // "plain" can be large (up to ~96 KiB); keep a dedicated document.
  DynamicJsonDocument doc(SD_TEXT_MAX_BYTES + 4096);
  if (!parse_body(doc)) {
    send_error(400, "JSON 格式錯誤或內容過大");
    return;
  }
  String path = doc["path"] | "";
  if (!doc.containsKey("content")) {
    send_error(400, "缺少 content");
    return;
  }
  String content = doc["content"] | "";
  if (path.length() == 0) {
    send_error(400, "缺少路徑");
    return;
  }
  String err;
  if (!sd_write_text(path, content, err)) {
    send_error(400, err);
    return;
  }
  send_json(200, String("{\"ok\":true,\"size\":") + (unsigned long)content.length() + "}");
}

static void api_sd_download() {
  String path = arg_or("path", "");
  if (path.length() == 0) {
    send_error(400, "缺少路徑");
    return;
  }
  String norm;
  if (!sd_norm(path, norm)) {
    send_error(400, "路徑不合法");
    return;
  }

  if (!bus_lock(5000)) {
    send_error(503, "SD 卡忙碌中");
    return;
  }

  File f = SD.open(sd_abs(norm), "r");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    bus_unlock();
    send_error(404, "找不到檔案");
    return;
  }

  String name = norm;
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);

  // inline=1 lets the web UI show <img> previews without forcing a download.
  if (arg_or("inline", "0") == "1") {
    s_server.sendHeader("Content-Disposition",
                        String("inline; filename*=UTF-8''") + rfc5987(name));
    s_server.sendHeader("Cache-Control", "private, max-age=600");
  } else {
    s_server.sendHeader("Content-Disposition",
                        String("attachment; filename*=UTF-8''") + rfc5987(name));
    s_server.sendHeader("Cache-Control", "no-store");
  }
  s_server.streamFile(f, mime_for(name));
  f.close();
  bus_unlock();
}

// ============================================================================
//  SD upload (multipart/form-data, one file per request)
// ============================================================================
static String sanitize_filename(const String &raw) {
  String name = raw;
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  slash = name.lastIndexOf('\\');
  if (slash >= 0) name = name.substring(slash + 1);
  name.trim();

  String out;
  out.reserve(name.length());
  for (size_t i = 0; i < name.length(); ++i) {
    char c = name[i];
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|' || (uint8_t)c < 0x20) {
      out += '_';
    } else {
      out += c;
    }
  }
  while (out.startsWith(".")) out.remove(0, 1);
  if (out.length() > 120) out = out.substring(0, 120);
  return out;
}

static void api_sd_upload_chunk() {
  HTTPUpload &up = s_server.upload();

  if (up.status == UPLOAD_FILE_START) {
    s_upload_ok       = false;
    s_upload_active   = true;
    s_upload_error    = "";
    s_upload_bytes    = 0;
    s_upload_declared = up.totalSize;
    s_upload_path     = "";
    s_upload_dir      = "/";

    String dir = s_server.header("X-Upload-Dir");
    if (dir.length() == 0) dir = s_server.header("X-Upload-Path");
    if (dir.length() == 0) dir = s_server.arg("dir");
    if (dir.length() == 0) dir = s_server.arg("path");
    if (dir.length()) {
      String norm;
      if (sd_norm(dir, norm)) s_upload_dir = norm;
    }

    String name = sanitize_filename(up.filename);
    if (name.length() == 0) {
      s_upload_error = "檔名不合法";
      return;
    }

    String combined = (s_upload_dir == "/") ? ("/" + name) : (s_upload_dir + "/" + name);
    String norm;
    if (!sd_norm(combined, norm)) {
      s_upload_error = "路徑不合法";
      return;
    }

    if (!sd_ready()) {
      s_upload_error = "SD 卡未掛載";
      return;
    }
    String derr;
    if (s_upload_dir != "/" && !sd_ensure_dir(s_upload_dir, derr)) {
      s_upload_error = derr;
      return;
    }

    if (!bus_lock(5000)) {
      s_upload_error = "SD 卡忙碌中";
      return;
    }
    s_upload_file = SD.open(sd_abs(norm), "w");
    if (!s_upload_file) {
      s_upload_error = "無法建立檔案";
    } else {
      s_upload_path = norm;
      s_upload_ok = true;
    }
    bus_unlock();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!s_upload_ok) return;
    if (s_upload_bytes + up.currentSize > UPLOAD_MAX_BYTES) {
      s_upload_ok = false;
      s_upload_error = "檔案超過大小上限";
      return;
    }
    if (!bus_lock(5000)) {
      s_upload_ok = false;
      s_upload_error = "SD 卡忙碌中";
      return;
    }
    size_t written = s_upload_file.write(up.buf, up.currentSize);
    bus_unlock();
    if (written != up.currentSize) {
      s_upload_ok = false;
      s_upload_error = "寫入失敗（SD 卡已滿？）";
      return;
    }
    s_upload_bytes += written;
  } else if (up.status == UPLOAD_FILE_END) {
    if (s_upload_file) {
      bus_lock(5000);
      s_upload_file.flush();
      s_upload_file.close();
      bus_unlock();
    }
    if (s_upload_ok && s_upload_declared && s_upload_bytes != s_upload_declared) {
      s_upload_ok = false;
      s_upload_error = "上傳不完整";
    }
    if (!s_upload_ok && s_upload_path.length()) {
      String err;
      sd_remove(s_upload_path, err);   // never leave half written files behind
    }
    s_upload_active = false;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (s_upload_file) {
      bus_lock(5000);
      s_upload_file.close();
      bus_unlock();
    }
    if (s_upload_path.length()) {
      String err;
      sd_remove(s_upload_path, err);
    }
    s_upload_ok = false;
    s_upload_active = false;
    s_upload_error = "上傳已取消";
  }
}

static void api_sd_upload_done() {
  if (s_upload_active) {
    send_error(409, "上傳尚未完成");
    return;
  }
  if (s_upload_ok) {
    String out = "{\"ok\":true,\"path\":\"" + json_escape(s_upload_path) +
                 "\",\"size\":" + u64_to_string(s_upload_bytes) + "}";
    send_json(200, out);
    return;
  }
  send_error(400, s_upload_error.length() ? s_upload_error : "沒有收到檔案");
}

// ============================================================================
//  Wi-Fi
// ============================================================================
static void api_wifi_status() {
  send_json(200, String("{\"ok\":true,\"wifi\":") + wifi_status_json() + "}");
}

static void api_wifi_scan() {
  send_json(200, wifi_scan_json());
}

static void api_wifi_connect() {
  DynamicJsonDocument doc(2048);
  if (!parse_body(doc)) {
    send_error(400, "JSON 格式錯誤");
    return;
  }
  String ssid = doc["ssid"] | "";
  String password = doc["password"] | "";
  ssid.trim();
  if (ssid.length() == 0) {
    // Some clients send the password as "pass" instead of "password".
    password = doc["pass"] | "";
  }
  String err;
  if (!wifi_connect(ssid, password, err)) {
    send_error(400, err);
    return;
  }
  send_json(200, String("{\"ok\":true,\"ip\":\"") + WiFi.localIP().toString() + "\"}");
}

static void api_wifi_forget() {
  String err;
  if (!wifi_forget(err)) {
    send_error(400, err);
    return;
  }
  send_ok();
}

// ============================================================================
//  configuration
// ============================================================================
static void api_config_get() {
  send_json(200, String("{\"ok\":true,\"config\":") + config_to_json(false) + "}");
}

static void api_config_post() {
  DynamicJsonDocument doc(CONFIG_MAX_BYTES);
  if (!parse_body(doc)) {
    send_error(400, "JSON 格式錯誤");
    return;
  }

  AppConfig before;
  config_snapshot(before);

  String err;
  if (!config_apply_json(doc.as<JsonVariantConst>(), err, true)) {
    send_error(400, err.length() ? err : "設定儲存失敗");
    return;
  }

  AppConfig after;
  config_snapshot(after);

  display_apply_config();

  bool reboot = (before.device_name != after.device_name) ||
                (before.ap_password != after.ap_password);

  String out = "{\"ok\":true,\"reboot_required\":";
  out += reboot ? "true" : "false";
  out += "}";
  send_json(200, out);
}

// ============================================================================
//  chat / voice jobs
// ============================================================================
static void api_chat() {
  DynamicJsonDocument doc(12288);
  if (!parse_body(doc)) {
    send_error(400, "JSON 格式錯誤");
    return;
  }
  String message = doc["message"] | "";
  message.trim();
  if (message.length() == 0) {
    send_error(400, "訊息是空的");
    return;
  }

  String err;
  if (!jobs_start_chat(message, err)) {
    send_error(409, err);
    return;
  }
  display_set_speech("");
  send_json(200, jobs_status_json());
}

static void api_voice() {
  DynamicJsonDocument doc(1024);
  int seconds = 0;
  if (parse_body(doc)) seconds = doc["seconds"] | 0;

  String err;
  if (!jobs_start_voice(seconds, err)) {
    send_error(409, err);
    return;
  }
  send_json(200, jobs_status_json());
}

static void api_job() {
  send_json(200, jobs_status_json());
}

static void api_chat_reset() {
  chat_reset();
  display_clear_speech();
  display_set_face(FACE_IDLE);
  send_ok();
}

static void api_chat_history() {
  send_json(200, chat_history_json());
}

// ============================================================================
//  display / face
// ============================================================================
static void api_face() {
  DynamicJsonDocument doc(512);
  if (!parse_body(doc)) {
    send_error(400, "JSON 格式錯誤");
    return;
  }
  String name = doc["face"] | "";
  if (name.length() == 0) {
    send_error(400, "缺少 face");
    return;
  }
  FaceExpression expr = display_face_from_name(name);
  display_set_face(expr);
  send_json(200, String("{\"ok\":true,\"face\":\"") + display_face_name(expr) + "\"}");
}

static void api_display() {
  DynamicJsonDocument doc(1024);
  if (!parse_body(doc)) {
    send_error(400, "JSON 格式錯誤");
    return;
  }

  String err;
  if (!config_apply_json(doc.as<JsonVariantConst>(), err, true)) {
    send_error(400, err.length() ? err : "顯示設定儲存失敗");
    return;
  }
  display_apply_config();

  AppConfig cfg;
  config_snapshot(cfg);
  String out = "{\"ok\":true,\"brightness\":";
  out += cfg.brightness;
  out += ",\"rotation\":";
  out += cfg.rotation;
  out += ",\"face_enabled\":";
  out += cfg.face_enabled ? "true" : "false";
  out += "}";
  send_json(200, out);
}

// ============================================================================
//  reboot
// ============================================================================
static void api_reboot() {
  send_json(200, "{\"ok\":true,\"message\":\"裝置重新啟動中\"}");
  s_reboot_pending = true;
  s_reboot_at = millis() + 400;
}

// ============================================================================
//  static files from SPIFFS
// ============================================================================
static const char FALLBACK_HTML[] PROGMEM =
    "<!doctype html><html lang=\"zh-Hant\"><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>T-Camera Plus</title>"
    "<style>body{background:#0b1020;color:#e6eefc;font-family:system-ui,sans-serif;"
    "padding:32px;line-height:1.7}h1{font-size:20px}code{background:#16213a;"
    "padding:2px 6px;border-radius:4px}</style>"
    "<h1>尚未上傳網頁介面</h1>"
    "<p>請用 Arduino IDE 的 <b>ESP32 Sketch Data Upload</b> 外掛上傳 "
    "<code>data/</code> 資料夾到 SPIFFS，然後重新整理本頁。</p>"
    "<p>API 仍可使用，例如 <a style=\"color:#2dd4ff\" href=\"/api/status\">/api/status</a>。</p>"
    "</html>";

static bool serve_spiffs_file(const String &path) {
  if (!spiffs_ready()) return false;
  if (path.indexOf("..") >= 0) return false;
  File f = SPIFFS.open(path, "r");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    return false;
  }
  s_server.sendHeader("Cache-Control", "public, max-age=60");
  s_server.streamFile(f, mime_for(path));
  f.close();
  return true;
}

static void handle_not_found() {
  String uri = s_server.uri();
  if (uri.startsWith("/api/")) {
    send_error(404, "找不到 API 端點");
    return;
  }
  if (s_server.method() != HTTP_GET) {
    send_error(405, "不支援的請求方法");
    return;
  }

  if (uri == "/") uri = "/index.html";
  if (serve_spiffs_file(uri)) return;

  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send_P(200, "text/html; charset=utf-8", FALLBACK_HTML);
}

// ============================================================================
//  setup
// ============================================================================
void web_begin() {
  const char *header_keys[] = {"X-Upload-Dir", "X-Upload-Path"};
  s_server.collectHeaders(header_keys, 2);

  s_server.on("/api/status",        HTTP_GET,  api_status);
  s_server.on("/api/spiffs",        HTTP_GET,  api_spiffs);

  s_server.on("/api/sd/list",       HTTP_GET,  api_sd_list);
  s_server.on("/api/sd/read",       HTTP_GET,  api_sd_read);
  s_server.on("/api/sd/download",   HTTP_GET,  api_sd_download);
  s_server.on("/api/sd/mkdir",      HTTP_POST, api_sd_mkdir);
  s_server.on("/api/sd/delete",     HTTP_POST, api_sd_delete);
  s_server.on("/api/sd/rename",     HTTP_POST, api_sd_rename);
  s_server.on("/api/sd/write",      HTTP_POST, api_sd_write);
  s_server.on("/api/sd/upload",     HTTP_POST, api_sd_upload_done, api_sd_upload_chunk);

  s_server.on("/api/wifi/status",   HTTP_GET,  api_wifi_status);
  s_server.on("/api/wifi/scan",     HTTP_GET,  api_wifi_scan);
  s_server.on("/api/wifi/scan",     HTTP_POST, api_wifi_scan);
  s_server.on("/api/wifi/connect",  HTTP_POST, api_wifi_connect);
  s_server.on("/api/wifi/forget",   HTTP_POST, api_wifi_forget);

  s_server.on("/api/config",        HTTP_GET,  api_config_get);
  s_server.on("/api/config",        HTTP_POST, api_config_post);

  s_server.on("/api/chat",          HTTP_POST, api_chat);
  s_server.on("/api/chat/reset",    HTTP_POST, api_chat_reset);
  s_server.on("/api/chat/history",  HTTP_GET,  api_chat_history);
  s_server.on("/api/voice",         HTTP_POST, api_voice);
  s_server.on("/api/job",           HTTP_GET,  api_job);

  s_server.on("/api/face",          HTTP_POST, api_face);
  s_server.on("/api/display",       HTTP_POST, api_display);

  s_server.on("/api/reboot",        HTTP_POST, api_reboot);

  s_server.onNotFound(handle_not_found);
  s_server.begin();

  Serial.printf("[web] http://%s/  (AP http://%s/)\n",
                WiFi.localIP().toString().c_str(), WiFi.softAPIP().toString().c_str());
}

void web_loop() {
  s_server.handleClient();
  if (s_reboot_pending && (int32_t)(millis() - s_reboot_at) >= 0) {
    Serial.println("[web] rebooting on request");
    delay(100);
    ESP.restart();
  }
}
