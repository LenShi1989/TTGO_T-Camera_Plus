#include "webui.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "app_config.h"
#include "audio.h"
#include "camera_face.h"
#include "gemini.h"
#include "img_converters.h"
#include "power.h"
#include "storage.h"
#include "util.h"
#include "wifi_manager.h"

// 前端檔案（index.html / style.css / app.js）放在 SPIFFS，
// 用 Arduino IDE 的「ESP32 Sketch Data Upload」上傳 data/ 資料夾。
// 所有裝置參數仍然只存在 SD 卡 /config.json。

static WebServer server(80);
static bool s_active = false;
static bool s_spiffs = false;

#define SD_LIST_MAX 48
#define SD_NAME_MAX 48
#define VOICE_MAX_BYTES (320 * 1024)
#define SD_EDIT_MAX_BYTES (96 * 1024)

// ---------------------------------------------------------------- 小工具

static bool checkAuth() {
  if (g_cfg.web.user.isEmpty()) return true;
  if (server.authenticate(g_cfg.web.user.c_str(), g_cfg.web.password.c_str())) return true;
  server.requestAuthentication();
  return false;
}

static void sendJson(int code, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}

static String okJson() { return "{\"ok\":true}"; }

static String errJson(const String &msg) {
  return "{\"ok\":false,\"error\":\"" + jsonEscape(msg) + "\"}";
}

static String normPath(String path) {
  if (path.isEmpty()) return String("/");
  if (!path.startsWith("/")) path = "/" + path;
  while (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
  return path;
}

static String baseName(const String &path) {
  int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

static String parentPath(const String &path) {
  if (path == "/" || path.isEmpty()) return String("/");
  int slash = path.lastIndexOf('/');
  if (slash <= 0) return String("/");
  return path.substring(0, slash);
}

static const char *mimeFor(const String &path) {
  if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".ico")) return "image/x-icon";
  return "text/plain; charset=utf-8";
}

// ---------------------------------------------------------------- 靜態檔案（SPIFFS）

static const char kNoDataHtml[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SPIFFS 尚未上傳</title>
<style>
body{margin:0;background:#0b0e16;color:#e8eef8;font-family:system-ui,"Noto Sans TC",sans-serif;line-height:1.8;padding:26px}
h1{font-size:19px;color:#00c8ff}code{background:#161b28;border:1px solid #252d40;padding:2px 7px;border-radius:6px}
ol{padding-left:22px}li{margin:6px 0}
</style></head><body>
<h1>找不到 /index.html</h1>
<p>網頁介面存放在 SPIFFS，尚未上傳。請用 Arduino IDE：</p>
<ol>
<li>確認已安裝 <b>ESP32 Sketch Data Upload</b>（ESP32FS）工具</li>
<li>Tools → <b>ESP32 Sketch Data Upload</b>（會上傳 <code>data/</code> 資料夾）</li>
<li>上傳完成後重新整理本頁</li>
</ol>
<p>開發板設定需為 <code>Huge APP (3MB No OTA/1MB SPIFFS)</code>，否則沒有 SPIFFS 分區。</p>
<p>在這之前，USB 序列埠（115200）的 <code>help</code> 指令仍可操作裝置。</p>
</body></html>)HTML";

static bool serveSpiffsFile(const String &path) {
  if (!s_spiffs) return false;
  File f = SPIFFS.open(path.c_str(), "r");
  if (!f) return false;
  if (f.isDirectory()) {
    f.close();
    return false;
  }
  server.sendHeader("Cache-Control", "no-cache");
  server.streamFile(f, mimeFor(path));
  f.close();
  return true;
}

// ---------------------------------------------------------------- SPIFFS 資訊

static void handleFs() {
  if (!checkAuth()) return;
  uint32_t total = 0, used = 0;
  if (s_spiffs) {
    total = SPIFFS.totalBytes();
    used = SPIFFS.usedBytes();
  }

  String s;
  s.reserve(1024);
  s += "{\"mounted\":";
  s += s_spiffs ? "true" : "false";
  s += ",\"total\":";
  s += String((unsigned long)total);
  s += ",\"used\":";
  s += String((unsigned long)used);
  s += ",\"files\":[";

  if (s_spiffs) {
    File dir = SPIFFS.open("/", "r");
    bool first = true;
    if (dir) {
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) continue;
        String name = f.name();
        if (!name.startsWith("/")) name = "/" + name;
        if (!first) s += ",";
        first = false;
        s += "{\"name\":\"" + jsonEscape(name) + "\",\"size\":" +
             String((unsigned long)f.size()) + "}";
      }
      dir.close();
    }
  }
  s += "]}";
  sendJson(200, s);
}

// ---------------------------------------------------------------- SD 卡

struct SdEntry {
  char name[SD_NAME_MAX];
  uint32_t size;
  bool dir;
};

static int entryCmp(const SdEntry &a, const SdEntry &b) {
  if (a.dir != b.dir) return a.dir ? -1 : 1;
  return strcasecmp(a.name, b.name);
}

static void handleSdList() {
  if (!checkAuth()) return;
  if (!storageReady()) {
    sendJson(503, errJson("SD 卡未掛載"));
    return;
  }
  String path = normPath(server.arg("path"));
  File dir = SD.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    sendJson(404, errJson("路徑不存在或不是資料夾"));
    return;
  }

  static SdEntry entries[SD_LIST_MAX];
  int n = 0;
  bool more = false;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (n >= SD_LIST_MAX) {
      more = true;
      break;
    }
    String nm = baseName(String(f.name()));
    if (nm.isEmpty()) continue;
    nm.toCharArray(entries[n].name, SD_NAME_MAX);
    entries[n].dir = f.isDirectory();
    entries[n].size = entries[n].dir ? 0 : (uint32_t)f.size();
    n++;
  }
  dir.close();

  for (int i = 1; i < n; i++) {
    SdEntry tmp = entries[i];
    int j = i - 1;
    while (j >= 0 && entryCmp(entries[j], tmp) > 0) {
      entries[j + 1] = entries[j];
      j--;
    }
    entries[j + 1] = tmp;
  }

  uint64_t total = 0, used = 0;
  storageInfo(total, used);

  String s;
  s.reserve(512 + n * 64);
  s += "{\"ok\":true,\"path\":\"" + jsonEscape(path) + "\"";
  s += ",\"parent\":\"" + jsonEscape(parentPath(path)) + "\"";
  s += ",\"total\":" + String((unsigned long)total);
  s += ",\"used\":" + String((unsigned long)used);
  s += ",\"more\":";
  s += more ? "true" : "false";
  s += ",\"entries\":[";
  for (int i = 0; i < n; i++) {
    if (i) s += ",";
    s += "{\"name\":\"" + jsonEscape(String(entries[i].name)) + "\"";
    s += ",\"size\":" + String((unsigned long)entries[i].size);
    s += ",\"dir\":";
    s += entries[i].dir ? "true" : "false";
    s += "}";
  }
  s += "]}";
  sendJson(200, s);
}

static void handleSdRead() {
  if (!checkAuth()) return;
  if (!storageReady()) {
    sendJson(503, errJson("SD 卡未掛載"));
    return;
  }
  String path = normPath(server.arg("path"));
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    sendJson(404, errJson("找不到檔案"));
    return;
  }
  size_t size = f.size();
  if (size > SD_EDIT_MAX_BYTES) {
    f.close();
    sendJson(413, errJson("檔案太大，請直接下載"));
    return;
  }
  char *raw = (char *)ps_malloc(size + 1);
  if (!raw) raw = (char *)malloc(size + 1);
  if (!raw) {
    f.close();
    sendJson(500, errJson("記憶體不足"));
    return;
  }
  size_t got = f.read((uint8_t *)raw, size);
  f.close();
  raw[got] = 0;

  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/plain; charset=utf-8", raw, got);
  free(raw);
}

static void handleSdDownload() {
  if (!checkAuth()) return;
  if (!storageReady()) {
    server.send(503, "text/plain; charset=utf-8", "SD 卡未掛載");
    return;
  }
  String path = normPath(server.arg("path"));
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    server.send(404, "text/plain; charset=utf-8", "找不到檔案");
    return;
  }
  server.sendHeader("Content-Disposition",
                    "attachment; filename=\"" + baseName(path) + "\"");
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

static void handleSdWrite() {
  if (!checkAuth()) return;
  if (!storageReady()) {
    sendJson(503, errJson("SD 卡未掛載"));
    return;
  }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, server.arg("plain"));
  if (e) {
    sendJson(400, errJson("JSON 解析失敗"));
    return;
  }
  String path = normPath(doc["path"] | "");
  String data = doc["data"] | "";
  bool append = doc["append"] | false;
  if (path == "/") {
    sendJson(400, errJson("缺少檔案路徑"));
    return;
  }

  File f = SD.open(path.c_str(), append ? FILE_APPEND : FILE_WRITE);
  if (!f) {
    sendJson(500, errJson("無法寫入 " + path));
    return;
  }
  size_t written = data.length() ? f.write((const uint8_t *)data.c_str(), data.length()) : 0;
  f.close();
  if (written != data.length()) {
    sendJson(500, errJson("寫入不完整"));
    return;
  }
  logLine("SD", "已儲存 %s（%u bytes）", path.c_str(), (unsigned)written);
  sendJson(200, "{\"ok\":true,\"path\":\"" + jsonEscape(path) + "\",\"size\":" +
                    String((unsigned long)written) + "}");
}

static void handleSdDelete() {
  if (!checkAuth()) return;
  if (!storageReady()) {
    sendJson(503, errJson("SD 卡未掛載"));
    return;
  }
  String path = normPath(server.arg("path"));
  if (path == "/") {
    sendJson(400, errJson("不能刪除根目錄"));
    return;
  }
  File f = SD.open(path.c_str());
  bool isDir = f && f.isDirectory();
  if (f) f.close();
  bool ok = isDir ? SD.rmdir(path.c_str()) : SD.remove(path.c_str());
  if (ok) {
    logLine("SD", "已刪除 %s", path.c_str());
    sendJson(200, okJson());
  } else {
    sendJson(500, errJson(isDir ? "資料夾必須是空的" : "刪除失敗"));
  }
}

static void handleSdMkdir() {
  if (!checkAuth()) return;
  if (!storageReady()) {
    sendJson(503, errJson("SD 卡未掛載"));
    return;
  }
  String path = normPath(server.arg("path"));
  if (path == "/") {
    sendJson(400, errJson("缺少資料夾名稱"));
    return;
  }
  if (SD.exists(path.c_str())) {
    sendJson(409, errJson("已存在同名項目"));
    return;
  }
  if (SD.mkdir(path.c_str())) {
    sendJson(200, okJson());
  } else {
    sendJson(500, errJson("建立資料夾失敗"));
  }
}

// ---- 上傳（multipart，邊收邊寫入 SD 卡）----

static File s_upFile;
static bool s_upOpen = false;
static bool s_upFailed = false;

static void sdUploadData() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    s_upFailed = false;
    String dir = normPath(server.arg("path"));
    String name = baseName(String(up.filename));
    if (name.isEmpty() || !storageReady()) {
      s_upFailed = true;
      return;
    }
    if (!SD.exists(dir.c_str())) SD.mkdir(dir.c_str());
    String full = dir == "/" ? "/" + name : dir + "/" + name;
    s_upFile = SD.open(full.c_str(), FILE_WRITE);
    s_upOpen = (bool)s_upFile;
    if (!s_upOpen) {
      s_upFailed = true;
      logLine("SD", "上傳失敗：無法建立 %s", full.c_str());
    } else {
      logLine("SD", "上傳中：%s", full.c_str());
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_upFailed || !s_upOpen) return;
    if (s_upFile.write(up.buf, up.currentSize) != up.currentSize) s_upFailed = true;
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (s_upOpen) {
      s_upFile.close();
      s_upOpen = false;
    }
    if (up.status == UPLOAD_FILE_ABORTED) s_upFailed = true;
  }
}

static void handleSdUploadDone() {
  if (!checkAuth()) return;
  if (!storageReady()) {
    sendJson(503, errJson("SD 卡未掛載"));
    return;
  }
  if (s_upOpen) {
    s_upFile.close();
    s_upOpen = false;
  }
  if (s_upFailed) {
    sendJson(500, errJson("上傳失敗（容量不足或檔名不合法）"));
    return;
  }
  sendJson(200, okJson());
}

// ---------------------------------------------------------------- 語音（瀏覽器麥克風）

static uint8_t *s_voiceBuf = nullptr;
static size_t s_voiceLen = 0;
static size_t s_voiceCap = 0;
static bool s_voiceBad = false;

static void voiceUploadData() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    s_voiceLen = 0;
    s_voiceBad = false;
    if (s_voiceCap < VOICE_MAX_BYTES) {
      if (s_voiceBuf) {
        free(s_voiceBuf);
        s_voiceBuf = nullptr;
      }
      s_voiceBuf = (uint8_t *)ps_malloc(VOICE_MAX_BYTES);
      if (!s_voiceBuf) s_voiceBuf = (uint8_t *)malloc(VOICE_MAX_BYTES);
      s_voiceCap = s_voiceBuf ? VOICE_MAX_BYTES : 0;
    }
    if (!s_voiceBuf) {
      s_voiceBad = true;
      logLine("AI", "語音緩衝區配置失敗");
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_voiceBad) return;
    if (s_voiceLen + up.currentSize > s_voiceCap) {
      s_voiceBad = true;
      return;
    }
    memcpy(s_voiceBuf + s_voiceLen, up.buf, up.currentSize);
    s_voiceLen += up.currentSize;
  }
}

static void handleVoice() {
  if (!checkAuth()) return;
  if (!appCanRunAction()) {
    sendJson(409, errJson("裝置忙碌中，請先按「TFT 回到待機畫面」再試"));
    return;
  }
  if (!geminiReady()) {
    sendJson(503, errJson("尚未設定 AI 端點（config.json 的 ai.api_url）"));
    return;
  }
  if (!wifiConnected()) {
    sendJson(503, errJson("WiFi 未連線"));
    return;
  }
  if (s_voiceBad || !s_voiceBuf || s_voiceLen < 128) {
    sendJson(400, errJson("沒有收到音訊資料"));
    return;
  }
  if (memcmp(s_voiceBuf, "RIFF", 4) != 0) {
    sendJson(400, errJson("只接受 WAV（PCM）格式"));
    return;
  }

  logLine("AI", "收到瀏覽器語音 %u bytes，送出 Gemini", (unsigned)s_voiceLen);

  // TFT 同步顯示球體動畫（與本機「麥克風收音」一致）
  appOrbThinking();

  g_appBusy = true;
  AiReply r;
  bool ok = appAiRun(true, String(), s_voiceBuf, s_voiceLen, r, appOrbTick);
  g_appBusy = false;

  if (!ok) {
    sendJson(200, errJson(r.error));
    return;
  }
  appAddHistory(r.user, r.reply);
  // 讓 TFT 同步顯示球體動畫與回覆
  if (appCanRunAction()) appRunAction(ACT_AI_ORB);

  String out = "{\"ok\":true,\"user\":\"" + jsonEscape(r.user) + "\",\"reply\":\"" +
               jsonEscape(r.reply) + "\"}";
  sendJson(200, out);
}

// ---------------------------------------------------------------- 板載麥克風（I2S）

// 錄音時讓 TFT 球體持續轉動（audioRecord 每 80ms 呼叫一次）
static void micProgress(int level, uint32_t elapsedMs) {
  (void)level;
  (void)elapsedMs;
  appOrbTick();
}

// GET  /api/mic          → 目前開關狀態
// POST /api/mic?on=1/0   → 開啟 / 關閉板載 I2S 麥克風
static void handleMic() {
  if (!checkAuth()) return;
  if (server.method() == HTTP_GET) {
    sendJson(200, String("{\"ok\":true,\"on\":") +
                      (g_cfg.audio.enabled ? "true" : "false") + ",\"ready\":" +
                      (audioReady() ? "true" : "false") + "}");
    return;
  }
  if (!appCanRunAction()) {
    sendJson(409, errJson("裝置忙碌中，請稍候再試"));
    return;
  }

  String on = server.arg("on");
  bool want = (on == "1" || on == "true" || on == "on");
  if (!want) {
    g_cfg.audio.enabled = false;
    audioEnd();
    g_cfg.save();
    logLine("MIC", "網頁關閉板載麥克風");
    sendJson(200, "{\"ok\":true,\"on\":false,\"ready\":false}");
    return;
  }
  if (audioBegin()) {
    g_cfg.audio.enabled = true;
    g_cfg.save();
    logLine("MIC", "網頁開啟板載麥克風");
    sendJson(200, "{\"ok\":true,\"on\":true,\"ready\":true}");
  } else {
    sendJson(500, errJson("I2S 麥克風初始化失敗（接腳 14 / 32 / 33）"));
  }
}

// POST /api/mic/record → 用板載麥克風錄音並直接送 Gemini
static void handleMicRecord() {
  if (!checkAuth()) return;
  if (!appCanRunAction()) {
    sendJson(409, errJson("裝置忙碌中，請稍候再試"));
    return;
  }
  if (!geminiReady()) {
    sendJson(503, errJson("尚未設定 AI 端點（config.json 的 ai.api_url）"));
    return;
  }
  if (!wifiConnected()) {
    sendJson(503, errJson("WiFi 未連線"));
    return;
  }
  if (!g_cfg.audio.enabled) {
    sendJson(409, errJson("板載麥克風已關閉，請先切換麥克風開關到 ON"));
    return;
  }
  if (!audioReady() && !audioBegin()) {
    sendJson(409, errJson("板載麥克風初始化失敗，請確認接腳 14 / 32 / 33"));
    return;
  }

  uint32_t rate = g_cfg.audio.sample_rate;
  size_t maxSamples = (size_t)rate * g_cfg.audio.max_record_ms / 1000;
  if (maxSamples < rate / 2) maxSamples = rate / 2;

  int16_t *pcm = (int16_t *)ps_malloc(maxSamples * sizeof(int16_t));
  if (!pcm) pcm = (int16_t *)malloc(maxSamples * sizeof(int16_t));
  if (!pcm) {
    sendJson(500, errJson("記憶體不足，無法配置錄音緩衝區"));
    return;
  }

  appOrbThinking();
  g_appBusy = true;
  size_t got = audioRecord(pcm, maxSamples, g_cfg.audio.max_record_ms, g_cfg.audio.min_record_ms,
                           g_cfg.audio.silence_ms, g_cfg.audio.vad_threshold, g_cfg.audio.gain,
                           micProgress);

  if (got < rate / 5) {
    g_appBusy = false;
    free(pcm);
    sendJson(400, errJson("沒有收到聲音，請靠近麥克風再試一次"));
    return;
  }

  size_t wavLen = 44 + got * 2;
  uint8_t *wav = (uint8_t *)ps_malloc(wavLen);
  if (!wav) wav = (uint8_t *)malloc(wavLen);
  if (!wav) {
    g_appBusy = false;
    free(pcm);
    sendJson(500, errJson("記憶體不足，無法組裝 WAV"));
    return;
  }
  wavHeader(wav, rate, 1, 16, (uint32_t)(got * 2));
  memcpy(wav + 44, pcm, got * 2);
  free(pcm);

  logLine("AI", "板載麥克風錄音 %u 樣本，送出 Gemini", (unsigned)got);

  AiReply r;
  bool ok = appAiRun(true, String(), wav, wavLen, r, appOrbTick);
  free(wav);
  g_appBusy = false;

  if (!ok) {
    sendJson(200, errJson(r.error));
    return;
  }
  appAddHistory(r.user, r.reply);
  if (appCanRunAction()) appRunAction(ACT_AI_ORB);
  sendJson(200, "{\"ok\":true,\"user\":\"" + jsonEscape(r.user) + "\",\"reply\":\"" +
                    jsonEscape(r.reply) + "\"}");
}

// ---------------------------------------------------------------- 相機

static void handleCamFrame() {
  if (!checkAuth()) return;
  if (!camBegin()) {
    server.sendHeader("X-Cam-Error", "init");
    server.send(503, "text/plain; charset=utf-8", "相機初始化失敗，5 秒後會自動重試");
    return;
  }

  // 零複製：直接把相機的 JPEG 緩衝區送出去，送完才歸還。
  // 舊版每張都 ps_malloc + memcpy，6~7 fps 下會造成堆積體破碎與不必要的延遲。
  camera_fb_t *fb = camGrabFrame();
  if (!fb) {
    server.sendHeader("X-Cam-Error", "grab");
    server.send(503, "text/plain; charset=utf-8", "擷取失敗，請稍後重試");
    return;
  }

  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("X-Cam-Bytes", String((unsigned)fb->len));
  server.sendHeader("X-Cam-Fps", String((unsigned)camFps()));
  server.sendHeader("X-Free-Heap", String((unsigned)(ESP.getFreeHeap() / 1024)));

  if (fb->format == PIXFORMAT_JPEG) {
    server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
    camReturnFrame(fb);
    return;
  }

  uint8_t *jpg = nullptr;
  size_t len = 0;
  bool ok = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format,
                    (uint8_t)g_cfg.camera.jpeg_quality, &jpg, &len);
  camReturnFrame(fb);
  if (!ok || !jpg) {
    server.sendHeader("X-Cam-Error", "encode");
    server.send(503, "text/plain; charset=utf-8", "JPEG 轉檔失敗");
    return;
  }
  server.send_P(200, "image/jpeg", (const char *)jpg, len);
  free(jpg);
}

static void handleCamFace() {
  if (!checkAuth()) return;
  if (!camBegin()) {
    sendJson(503, errJson("相機初始化失敗，5 秒後會自動重試"));
    return;
  }

  // 人臉偵測會佔住相機數百毫秒；同一結果在 900 ms 內重複使用，
  // 避免前端輪詢把即時影像的 fps 拉下來。
  static uint32_t s_faceStamp = 0;
  static bool s_faceValid = false;
  static String s_faceJson;
  uint32_t now = millis();
  if (!server.hasArg("fresh") && s_faceValid && (uint32_t)(now - s_faceStamp) < 900) {
    sendJson(200, s_faceJson);
    return;
  }

  bool recognize = server.arg("recognize") != "0";
  FaceResult r;
  faceDetect(r, recognize, false);

  String s;
  s.reserve(256);
  s += "{\"ok\":true,\"detected\":";
  s += r.detected ? "true" : "false";
  s += ",\"recognized\":";
  s += r.recognized ? "true" : "false";
  s += ",\"name\":\"" + jsonEscape(r.name) + "\"";
  s += ",\"score\":" + String(r.score, 3);
  s += ",\"threshold\":" + String(g_cfg.camera.rec_threshold, 2);
  s += ",\"faces\":" + String(faceCount());
  s += ",\"box\":[" + String(r.x) + "," + String(r.y) + "," + String(r.w) + "," +
       String(r.h) + "]";
  s += ",\"crop\":240}";
  s_faceJson = s;
  s_faceStamp = millis();
  s_faceValid = true;
  sendJson(200, s);
}

// ---------------------------------------------------------------- 既有 API

static void handleStatus() {
  if (!checkAuth()) return;
  sendJson(200, appStatusJson());
}

static void handleHistory() {
  if (!checkAuth()) return;
  if (server.method() == HTTP_DELETE) {
    appClearHistory();
    sendJson(200, okJson());
    return;
  }
  sendJson(200, appHistoryJson());
}

static void handleChat() {
  if (!checkAuth()) return;
  if (!appCanRunAction()) {
    sendJson(409, errJson("裝置忙碌中，請先按「TFT 回到待機畫面」再試"));
    return;
  }
  String text = server.arg("plain");
  text.trim();
  if (text.isEmpty()) {
    sendJson(400, errJson("訊息為空"));
    return;
  }
  if (!geminiReady()) {
    sendJson(503, errJson("尚未設定 AI 端點（config.json 的 ai.api_url）"));
    return;
  }
  if (!wifiConnected()) {
    sendJson(503, errJson("WiFi 未連線"));
    return;
  }

  // 文字聊天也同步在 TFT 顯示球體動畫，與本機麥克風收音一致
  appOrbThinking();
  g_appBusy = true;
  AiReply r;
  appAiRun(false, text, nullptr, 0, r, appOrbTick);
  g_appBusy = false;
  if (r.ok) {
    appAddHistory(text, r.reply);
    if (appCanRunAction()) appRunAction(ACT_AI_ORB);
    sendJson(200, "{\"ok\":true,\"reply\":\"" + jsonEscape(r.reply) + "\"}");
  } else {
    sendJson(200, errJson(r.error));
  }
}

static void handleFaces() {
  if (!checkAuth()) return;
  String s = "{\"faces\":[";
  for (int i = 0; i < faceCount(); i++) {
    if (i) s += ",";
    s += "\"" + jsonEscape(faceNameAt(i)) + "\"";
  }
  s += "],\"max\":" + String(g_cfg.camera.max_faces);
  s += ",\"threshold\":" + String(g_cfg.camera.rec_threshold, 2) + "}";
  sendJson(200, s);
}

static void handleFaceAdd() {
  if (!checkAuth()) return;
  if (!appCanRunAction()) {
    sendJson(409, errJson("裝置忙碌中，請稍候再試"));
    return;
  }
  String name = server.arg("name");
  name.trim();
  if (name.isEmpty()) name = "ID" + String(faceCount() + 1);
  if (!camBegin()) {
    sendJson(500, errJson("相機初始化失敗"));
    return;
  }
  appEnrollBegin(name);
  g_appBusy = true;
  String err;
  bool ok = faceEnroll(name, appEnrollTick, err);
  g_appBusy = false;
  appEnrollEnd(ok, ok ? "已加入 " + name : err, name);
  if (ok) {
    sendJson(200, "{\"ok\":true,\"name\":\"" + jsonEscape(name) + "\",\"faces\":" +
                      String(faceCount()) + "}");
  } else {
    sendJson(500, errJson(err));
  }
}

static void handleFaceDelete() {
  if (!checkAuth()) return;
  String name = server.arg("name");
  if (name.isEmpty()) {
    sendJson(400, errJson("缺少 name"));
    return;
  }
  if (faceRemove(name)) {
    sendJson(200, okJson());
  } else {
    sendJson(404, errJson("找不到此人臉 ID"));
  }
}

static void handleFaceExport() {
  if (!checkAuth()) return;
  String msg;
  bool ok = faceExportCsv(msg);
  sendJson(ok ? 200 : 500,
           String("{\"ok\":") + (ok ? "true" : "false") + ",\"msg\":\"" + jsonEscape(msg) +
               "\"}");
}

static void handleWifiScan() {
  if (!checkAuth()) return;
  String ssids[16];
  int rssi[16];
  bool secure[16];
  int n = wifiScan(ssids, rssi, secure, 16);
  String s = "{\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i) s += ",";
    s += "{\"ssid\":\"" + jsonEscape(ssids[i]) + "\",\"rssi\":" + String(rssi[i]) +
         ",\"secure\":" + (secure[i] ? "true" : "false") + "}";
  }
  s += "]}";
  sendJson(200, s);
}

static void handleWifiConnect() {
  if (!checkAuth()) return;
  String ssid = server.arg("ssid");
  String pass = server.arg("password");
  if (ssid.isEmpty()) {
    sendJson(400, errJson("缺少 SSID"));
    return;
  }
  String err;
  bool ok = wifiConnect(ssid, pass, g_cfg.wifi.connect_timeout_ms, &err);
  if (ok) {
    sendJson(200, "{\"ok\":true,\"ip\":\"" + wifiIP() + "\"}");
  } else {
    sendJson(200, errJson(err));
  }
}

static void handleWifiClear() {
  if (!checkAuth()) return;
  wifiClearCredentials();
  sendJson(200, okJson());
}

static void handleConfigGet() {
  if (!checkAuth()) return;
  sendJson(200, g_cfg.toJson(true));
}

static void handleConfigSet() {
  if (!checkAuth()) return;
  String body = server.arg("plain");
  String err;
  if (!g_cfg.fromJson(body, &err)) {
    sendJson(400, errJson(err));
    return;
  }
  bool ok = g_cfg.save();
  sendJson(200, String("{\"ok\":") + (ok ? "true" : "false") +
                   (ok ? "" : ",\"error\":\"寫入 SD 卡失敗\"") + "}");
}

static void handleSnapshot() {
  if (!checkAuth()) return;
  if (!camBegin()) {
    server.send(500, "text/plain; charset=utf-8", "相機初始化失敗");
    return;
  }
  uint8_t *jpg = nullptr;
  size_t len = 0;
  if (!camSnapshotJpeg(&jpg, &len) || !jpg) {
    server.send(500, "text/plain; charset=utf-8", "擷取失敗");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "image/jpeg", (const char *)jpg, len);
  free(jpg);
}

static void handleAction() {
  if (!checkAuth()) return;
  int code = server.arg("code").toInt();
  if (code <= 0) {
    sendJson(400, errJson("缺少 code"));
    return;
  }
  // 返回 / 中斷不受忙碌狀態限制：本板沒有實體按鍵，一定要能從網頁離開目前畫面
  if (code == ACT_BACK) {
    appRunAction(ACT_BACK);
    sendJson(200, okJson());
    return;
  }
  if (!appCanRunAction()) {
    sendJson(409, errJson("裝置忙碌中，請先在網頁按「停止」"));
    return;
  }
  appRunAction((uint16_t)code);
  sendJson(200, okJson());
}

// ---------------------------------------------------------------- 路由

static void handleNotFound() {
  if (server.method() == HTTP_GET) {
    String uri = server.uri();
    if (!uri.startsWith("/api/")) {
      if (uri == "/" || uri.isEmpty()) uri = "/index.html";
      if (!checkAuth()) return;
      if (serveSpiffsFile(uri)) return;
      if (uri == "/index.html") {
        server.send_P(200, "text/html; charset=utf-8", kNoDataHtml);
        return;
      }
    }
  }
  server.send(404, "text/plain; charset=utf-8", "找不到頁面");
}

bool webuiActive() { return s_active; }

bool webuiSpiffsReady() { return s_spiffs; }

void webuiBegin() {
  if (!g_cfg.web.enabled) {
    logLine("WEB", "設定中已停用網頁介面");
    return;
  }

  s_spiffs = SPIFFS.begin(true);
  if (s_spiffs) {
    logLine("WEB", "SPIFFS 就緒：%u / %u bytes", (unsigned)SPIFFS.usedBytes(),
            (unsigned)SPIFFS.totalBytes());
    if (!SPIFFS.exists("/index.html")) {
      logLine("WEB", "SPIFFS 內沒有 /index.html，請用「ESP32 Sketch Data Upload」上傳 data/");
    }
  } else {
    logLine("WEB", "SPIFFS 掛載失敗（分區需為 Huge APP），無法載入前端檔案");
  }

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/history", HTTP_DELETE, handleHistory);
  server.on("/api/chat", HTTP_POST, handleChat);

  server.on("/api/voice", HTTP_POST, handleVoice, voiceUploadData);
  server.on("/api/mic", HTTP_GET, handleMic);
  server.on("/api/mic", HTTP_POST, handleMic);
  server.on("/api/mic/record", HTTP_POST, handleMicRecord);

  server.on("/api/faces", HTTP_GET, handleFaces);
  server.on("/api/faces/add", HTTP_POST, handleFaceAdd);
  server.on("/api/faces/delete", HTTP_POST, handleFaceDelete);
  server.on("/api/faces/export", HTTP_POST, handleFaceExport);

  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi", HTTP_POST, handleWifiConnect);
  server.on("/api/wifi/clear", HTTP_POST, handleWifiClear);

  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigSet);

  server.on("/api/fs", HTTP_GET, handleFs);

  server.on("/api/sd/list", HTTP_GET, handleSdList);
  server.on("/api/sd/read", HTTP_GET, handleSdRead);
  server.on("/api/sd/download", HTTP_GET, handleSdDownload);
  server.on("/api/sd/write", HTTP_POST, handleSdWrite);
  server.on("/api/sd/delete", HTTP_POST, handleSdDelete);
  server.on("/api/sd/mkdir", HTTP_POST, handleSdMkdir);
  server.on("/api/sd/upload", HTTP_POST, handleSdUploadDone, sdUploadData);

  server.on("/api/camera/frame", HTTP_GET, handleCamFrame);
  server.on("/api/camera/face", HTTP_GET, handleCamFace);
  server.on("/api/snapshot", HTTP_GET, handleSnapshot);

  server.on("/api/action", HTTP_POST, handleAction);

  server.onNotFound(handleNotFound);

  server.begin();
  s_active = true;
  logLine("WEB", "網頁介面已啟動：http://%s/",
          wifiConnected() ? wifiIP().c_str() : WiFi.softAPIP().toString().c_str());
}

void webuiHandle() {
  if (!s_active) return;
  server.handleClient();
}
