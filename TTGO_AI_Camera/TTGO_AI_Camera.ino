// =====================================================================
//  TTGO T-Camera Plus — AI 相機助理
//  ESP32 / ST7789 240x240 / OV2640 / I2S 麥克風 / microSD
//
//  操作方式：本板沒有使用者按鍵，RST 直接接 EN。
//    - 所有操作都在 SPIFFS 內的網頁完成（側邊欄五大頁籤）。
//    - TFT 沒有選單，只顯示待機儀表板：WiFi / IP / SD / 人臉 / 麥克風。
//    - 網頁按下動作後 TFT 才切換到對應畫面，按「TFT 回到待機畫面」返回。
//    - 序列埠只輸出除錯訊息，不再接受指令。
// =====================================================================

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>

#include "app.h"
#include "app_config.h"
#include "audio.h"
#include "board_pins.h"
#include "camera_face.h"
#include "display.h"
#include "font.h"
#include "gemini.h"
#include "power.h"
#include "storage.h"
#include "util.h"
#include "webui.h"
#include "wifi_manager.h"

#define APP_VERSION "1.0.0"

// =====================================================================
//  全域狀態
// =====================================================================

volatile bool g_appBusy = false;

static volatile uint16_t s_pendingAction = 0;

// ---- 對話記錄 ----
#define HIST_TURNS 10
struct ChatTurn {
  String user;
  String reply;
};
static ChatTurn s_turns[HIST_TURNS];
static int s_turnCount = 0;
static int s_turnNext = 0;

// ---- 顯示亮度循環 ----
static const uint8_t kBrightLevels[] = {255, 170, 100, 40};
static int s_brightIdx = 0;

// ---- AI 回覆分頁 ----
#define REPLY_LINES 12
static String s_replyLines[REPLY_LINES];
static int s_replyLineCount = 0;
static int s_replyPage = 0;

// =====================================================================
//  小工具
// =====================================================================

static void drawHeader(const char *title) {
  g_disp.fillRect(0, 0, 240, 30, COL_PANEL);
  g_disp.fillRect(0, 29, 240, 1, COL_ACCENT2);
  g_disp.drawText(10, 7, title, COL_ACCENT, 1);
}

static void drawFooterHint(const char *text) {
  g_disp.fillRect(0, 218, 240, 22, COL_PANEL);
  g_disp.fillRect(0, 218, 240, 1, COL_PANEL2);
  if (text && *text) g_disp.drawText(8, 220, text, COL_DIM, 1);
}

static String uptimeText() {
  uint32_t s = millis() / 1000;
  uint32_t h = s / 3600;
  uint32_t m = (s % 3600) / 60;
  uint32_t sec = s % 60;
  if (h) return String(h) + "時" + String(m) + "分";
  if (m) return String(m) + "分" + String(sec) + "秒";
  return String(sec) + "秒";
}

static String humanBytes(uint64_t v) {
  if (v >= 1024ULL * 1024ULL) return String((double)v / (1024.0 * 1024.0), 1) + " MB";
  if (v >= 1024ULL) return String((double)v / 1024.0, 1) + " KB";
  return String((unsigned long)v) + " B";
}

// =====================================================================
//  待機儀表板
//  本板沒有實體按鍵：TFT 只顯示狀態，所有操作都由網頁觸發。
// =====================================================================

static bool s_idle = true;
static uint32_t s_idleStamp = 0;

static String clipText(const String &src, size_t maxChars) {
  if (src.length() <= maxChars) return src;
  if (maxChars <= 1) return src.substring(0, maxChars);
  return src.substring(0, maxChars - 1) + "…";
}

static void idleRow(int y, const char *label, const String &value, uint16_t color) {
  g_disp.drawText(10, y, label, COL_DIM, 1);
  g_disp.drawText(68, y, value.c_str(), color, 1);
}

static void renderIdle() {
  g_disp.clear(COL_BG);
  drawHeader("TTGO AI Camera");

  String net;
  String ip;
  uint16_t netColor = COL_TEXT;
  if (wifiConnected()) {
    net = g_cfg.wifi.ssid;
    ip = wifiIP();
  } else if (wifiApActive()) {
    net = "AP " + wifiApSsid();
    ip = WiFi.softAPIP().toString();
    netColor = COL_YELLOW;
  } else {
    net = "未連線";
    ip = "-";
    netColor = COL_RED;
  }

  idleRow(40, "WiFi", clipText(net, 19), netColor);
  idleRow(66, "IP", ip, COL_TEXT);

  uint64_t total = 0, used = 0;
  storageInfo(total, used);
  String sd = storageReady() ? humanBytes(used) + " / " + humanBytes(total) : String("未掛載");
  idleRow(92, "SD", sd, storageReady() ? COL_TEXT : COL_RED);

  idleRow(118, "人臉", String(faceCount()) + " 組", COL_TEXT);
  idleRow(144, "麥克風", g_cfg.audio.enabled ? "ON" : "OFF",
          g_cfg.audio.enabled ? COL_GREEN : COL_DIM);

  g_disp.drawTextCenter(182, ("待機中  " + uptimeText()).c_str(), COL_DIM, 1);

  if (wifiConnected() || wifiApActive()) {
    String url = "http://" + ip;
    drawFooterHint(url.c_str());
  } else {
    drawFooterHint("請由網頁操作");
  }
  g_disp.push();
}

void appShowIdle() {
  s_idle = true;
  s_idleStamp = millis();
  renderIdle();
}

void appIdleTick() {
  if (!s_idle || g_appBusy || s_pendingAction != 0) return;
  uint32_t now = millis();
  if ((uint32_t)(now - s_idleStamp) < 5000) return;
  s_idleStamp = now;
  renderIdle();
}

// =====================================================================
//  共用畫面元件
// =====================================================================

static void screenFrame(const char *title) {
  g_disp.clear(COL_BG);
  drawHeader(title);
  drawFooterHint("網頁可回到待機畫面");
}

static void screenMessage(const char *title, const char *body) {
  screenFrame(title);
  g_disp.drawTextWrapped(10, 44, 220, body, COL_TEXT, COL_BG, false, 1, 20, 8);
  g_disp.push();
}

// 單次結果畫面先停留一下，讓使用者看清楚；網頁送出新動作可提前打斷。
static void holdScreen(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms && s_pendingAction == 0) {
    webuiHandle();
    delay(10);
  }
}

static void pushAndWait(uint32_t ms) {
  g_disp.push();
  uint32_t start = millis();
  while (millis() - start < ms) {
    webuiHandle();
    delay(10);
  }
}

// =====================================================================
//  球體動畫
// =====================================================================

#define ORB_CX 120
#define ORB_CY 86
#define ORB_R 36
#define ORB_BOX 58

static void orbDraw(int level, int phase, uint16_t color) {
  int cx = ORB_CX, cy = ORB_CY;
  g_disp.fillRect(cx - ORB_BOX, cy - ORB_BOX, ORB_BOX * 2, ORB_BOX * 2, COL_BG);

  int pulse = (level * 10) / 100;
  int r = ORB_R + pulse;
  if (r > ORB_R + 8) r = ORB_R + 8;

  // 外光暈
  for (int i = 0; i < 5; i++) {
    g_disp.fillCircleAlpha(cx, cy, r + 8 - i * 2, color, 16);
  }
  // 本體
  g_disp.fillCircle(cx, cy, r, color);
  g_disp.fillCircleAlpha(cx, cy, r, COL_WHITE, 30);
  g_disp.fillCircleAlpha(cx - r / 3, cy - r / 3, r / 2, COL_WHITE, 80);
  g_disp.drawCircle(cx, cy, r + 1, color);

  // 環形音量柱
  int base = r + 4;
  int maxLen = (ORB_BOX - 4) - base;
  if (maxLen < 0) maxLen = 0;
  for (int k = 0; k < 32; k++) {
    float a = (k * 11.25f + phase) * 3.14159265f / 180.0f;
    int len = 2 + (level * maxLen) / 100;
    if (len > maxLen) len = maxLen;
    if (len <= 0) continue;
    int x0 = cx + (int)(cosf(a) * base);
    int y0 = cy + (int)(sinf(a) * base);
    int x1 = cx + (int)(cosf(a) * (base + len));
    int y1 = cy + (int)(sinf(a) * (base + len));
    g_disp.drawLine(x0, y0, x1, y1, color);
  }

  // 環繞衛星
  for (int k = 0; k < 3; k++) {
    float a = (phase * 2 + k * 120) * 3.14159265f / 180.0f;
    int ox = cx + (int)(cosf(a) * (r + 16));
    int oy = cy + (int)(sinf(a) * (r + 16));
    g_disp.fillCircle(ox, oy, 2, COL_ACCENT);
  }

  g_disp.pushRect(cx - ORB_BOX, cy - ORB_BOX, ORB_BOX * 2, ORB_BOX * 2);
}

static void chatStatus(const char *text, uint16_t color) {
  g_disp.fillRect(0, 146, 240, 20, COL_BG);
  g_disp.drawTextCenter(148, text, color, 1);
  g_disp.pushRect(0, 146, 240, 20);
}

// =====================================================================
//  文字換行
// =====================================================================

static int wrapText(const String &text, int maxW, String *out, int maxLines) {
  size_t len = text.length();
  size_t i = 0;
  int line = 0;
  String cur;
  int curW = 0;
  while (i < len && line < maxLines) {
    uint32_t cp = utf8Decode(text.c_str(), len, i);
    if (cp == '\r') continue;
    if (cp == '\n') {
      out[line++] = cur;
      cur = "";
      curW = 0;
      continue;
    }
    int cw = g_disp.charWidth(cp, 1);
    if (curW + cw > maxW && cur.length() > 0) {
      out[line++] = cur;
      cur = "";
      curW = 0;
      if (line >= maxLines) break;
    }
    cur += utf8Encode(cp);
    curW += cw;
  }
  if (line < maxLines && cur.length() > 0) out[line++] = cur;
  return line;
}

static void drawReplyPage() {
  g_disp.fillRect(0, 164, 240, 74, COL_BG);
  for (int i = 0; i < 4; i++) {
    int idx = s_replyPage * 4 + i;
    if (idx >= s_replyLineCount) break;
    g_disp.drawText(10, 168 + i * 18, s_replyLines[idx].c_str(), COL_TEXT, 1);
  }
  g_disp.pushRect(0, 164, 240, 76);
}

// =====================================================================
//  聊天畫面
// =====================================================================

static int s_chatLevel = 0;
static int s_chatPhase = 0;

static void chatAudioTick(int level, uint32_t elapsedMs) {
  (void)elapsedMs;
  s_chatLevel = level;
  s_chatPhase = (s_chatPhase + 18) % 360;
  orbDraw(s_chatLevel, s_chatPhase, COL_ACCENT);
}

static void chatAiTick() { appOrbTick(); }

// 給網頁用的球體動畫：handleVoice() 等待 Gemini 時每 30ms 呼叫一次，
// 讓 TFT 與本機「麥克風收音」有同樣的思考動畫。
void appOrbThinking() {
  g_disp.clear(COL_BG);
  g_disp.fillRect(0, 0, 240, 30, COL_PANEL);
  g_disp.fillRect(0, 29, 240, 1, COL_ACCENT2);
  g_disp.drawText(10, 7, "AI聊天機器人", COL_ACCENT, 1);
  g_disp.drawTextCenter(196, "小 T 思考中…", COL_PURPLE, 1);
  g_disp.push();
  s_chatLevel = 0;
  s_chatPhase = 0;
}

void appOrbTick() {
  s_chatPhase = (s_chatPhase + 14) % 360;
  orbDraw(0, s_chatPhase, COL_PURPLE);
}

static void chatIdleLoop(const char *status) {
  chatStatus(status, COL_ACCENT);
  uint32_t lastFrame = millis();
  uint32_t lastPage = millis();
  int phase = 0;
  int levelPhase = 0;
  while (s_pendingAction == 0) {
    uint32_t now = millis();
    if (now - lastFrame >= 70) {
      lastFrame = now;
      phase = (phase + 1) % 360;
      levelPhase = (int)(18.0f + 12.0f * sinf(now / 420.0f));
      orbDraw(levelPhase, phase, COL_ACCENT);
    }
    if (s_replyLineCount > 4 && now - lastPage >= 4000) {
      lastPage = now;
      s_replyPage++;
      if (s_replyPage * 4 >= s_replyLineCount) s_replyPage = 0;
      drawReplyPage();
    }
    webuiHandle();
    delay(5);
  }
}

static void chatPrepareScreen(const char *title) {
  g_disp.clear(COL_BG);
  drawHeader(title);
  g_disp.drawTextCenter(196, "小 T 語音助理", COL_DIM, 1);
  g_disp.push();
  s_replyLineCount = 0;
  s_replyPage = 0;
}

static void screenChat() {
  if (!geminiReady()) {
    screenMessage("AI聊天機器人",
                  "尚未設定 AI 端點。\n\n請編輯 SD 卡 /config.json 的 ai.api_url / "
                  "ai.api_key，或到網頁「AI 聊天機器人」頁籤的 AI 設定填入。");
    return;
  }
  if (!wifiConnected()) {
    screenMessage("AI聊天機器人",
                  "WiFi 尚未連線。\n\n請先進入「WiFi設定」完成連線，"
                  "或用手機連線本機 AP 後由網頁設定。");
    return;
  }
  if (!g_cfg.audio.enabled) {
    screenMessage("AI聊天機器人",
                  "板載麥克風目前已關閉。\n\n請到網頁「AI 聊天機器人」把麥克風開關切到 ON，"
                  "再回來使用語音對話。");
    return;
  }
  if (!audioBegin()) {
    screenMessage("AI聊天機器人", "麥克風初始化失敗（I2S）。");
    return;
  }

  size_t sampleRate = g_cfg.audio.sample_rate;
  size_t maxSamples = sampleRate * g_cfg.audio.max_record_ms / 1000;
  if (maxSamples < sampleRate / 2) maxSamples = sampleRate / 2;

  int16_t *pcm = (int16_t *)ps_malloc(maxSamples * sizeof(int16_t));
  if (!pcm) pcm = (int16_t *)malloc(maxSamples * sizeof(int16_t));
  if (!pcm) {
    screenMessage("AI聊天機器人", "記憶體不足，無法配置錄音緩衝區。");
    return;
  }

  chatPrepareScreen("AI聊天機器人");
  chatStatus("請說話…", COL_GREEN);
  s_chatLevel = 0;
  s_chatPhase = 0;

  size_t got = audioRecord(pcm, maxSamples, g_cfg.audio.max_record_ms, g_cfg.audio.min_record_ms,
                           g_cfg.audio.silence_ms, g_cfg.audio.vad_threshold, g_cfg.audio.gain,
                           chatAudioTick);

  if (got < sampleRate / 5) {
    free(pcm);
    chatStatus("沒有收到聲音", COL_RED);
    chatIdleLoop("沒有收到聲音，請再試一次");
    return;
  }

  size_t wavLen = 44 + got * 2;
  uint8_t *wav = (uint8_t *)ps_malloc(wavLen);
  if (!wav) wav = (uint8_t *)malloc(wavLen);
  if (!wav) {
    free(pcm);
    screenMessage("AI聊天機器人", "記憶體不足，無法組裝 WAV。");
    return;
  }
  wavHeader(wav, sampleRate, 1, 16, (uint32_t)(got * 2));
  memcpy(wav + 44, pcm, got * 2);
  free(pcm);

  chatStatus("思考中…", COL_PURPLE);

  g_appBusy = true;
  AiReply reply;
  bool ok = appAiRun(true, String(), wav, wavLen, reply, chatAiTick);
  g_appBusy = false;
  free(wav);

  if (!ok) {
    s_replyLineCount = wrapText(reply.error, 220, s_replyLines, REPLY_LINES);
    s_replyPage = 0;
    chatStatus("發生錯誤", COL_RED);
    drawReplyPage();
    chatIdleLoop("發生錯誤，請由網頁返回");
    return;
  }

  appAddHistory(reply.user, reply.reply);

  if (reply.user.length()) {
    String u = "你說：" + reply.user;
    u = utf8Truncate(u, 60);
    chatStatus(u.c_str(), COL_YELLOW);
  } else {
    chatStatus("小 T 回覆", COL_ACCENT);
  }

  s_replyLineCount = wrapText(reply.reply, 220, s_replyLines, REPLY_LINES);
  s_replyPage = 0;
  drawReplyPage();
  chatIdleLoop("網頁可回到待機畫面");
}

static void screenOrb() {
  g_disp.clear(COL_BG);
  drawHeader("TFT OLED顯示");
  g_disp.push();

  if (s_turnCount == 0) {
    chatStatus("尚無對話記錄", COL_DIM);
    chatIdleLoop("先到「麥克風收音」跟小 T 聊聊");
    return;
  }

  int idx = (s_turnNext - 1 + HIST_TURNS) % HIST_TURNS;
  String last = s_turns[idx].reply;
  String user = s_turns[idx].user;

  if (user.length()) {
    String u = "你說：" + user;
    u = utf8Truncate(u, 60);
    chatStatus(u.c_str(), COL_YELLOW);
  } else {
    chatStatus("最近一次回覆", COL_ACCENT);
  }

  s_replyLineCount = wrapText(last, 220, s_replyLines, REPLY_LINES);
  s_replyPage = 0;
  drawReplyPage();
  chatIdleLoop("網頁可回到待機畫面");
}

// =====================================================================
//  系統 / WiFi 資訊畫面
// =====================================================================

static void screenWifiInfo() {
  screenFrame("WiFi連線資訊");
  int y = 40;
  char buf[96];

  g_disp.drawText(10, y, "SSID", COL_DIM, 1);
  y += 18;
  g_disp.drawText(10, y, g_cfg.wifi.ssid.isEmpty() ? "（未設定）" : g_cfg.wifi.ssid.c_str(),
                  COL_TEXT, 1);
  y += 26;

  g_disp.drawText(10, y, "狀態", COL_DIM, 1);
  y += 18;
  g_disp.drawText(10, y, wifiStatusText().c_str(), wifiConnected() ? COL_GREEN : COL_RED, 1);
  y += 26;

  g_disp.drawText(10, y, "IP 位址", COL_DIM, 1);
  y += 18;
  g_disp.drawText(10, y, wifiConnected() ? wifiIP().c_str() : "-", COL_TEXT, 1);
  y += 26;

  g_disp.drawText(10, y, "訊號強度", COL_DIM, 1);
  y += 18;
  snprintf(buf, sizeof(buf), "%d dBm", wifiConnected() ? wifiRSSI() : 0);
  g_disp.drawText(10, y, buf, COL_TEXT, 1);
  y += 26;

  g_disp.drawText(10, y, "AP 熱點", COL_DIM, 1);
  y += 18;
  g_disp.drawText(10, y, wifiApActive() ? wifiApSsid().c_str() : "（未開啟）", COL_ACCENT, 1);
  g_disp.push();
}

static void screenSysStatus() {
  screenFrame("系統狀態");
  int y = 40;
  char buf[96];

  uint64_t total = 0, used = 0;
  storageInfo(total, used);

  g_disp.drawText(10, y, "記憶體", COL_DIM, 1);
  y += 17;
  snprintf(buf, sizeof(buf), "Heap %u KB / PSRAM %u KB", (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned)(ESP.getFreePsram() / 1024));
  g_disp.drawText(10, y, buf, COL_TEXT, 1);
  y += 22;

  g_disp.drawText(10, y, "SD 卡", COL_DIM, 1);
  y += 17;
  if (storageReady()) {
    snprintf(buf, sizeof(buf), "%s / %s", humanBytes(used).c_str(), humanBytes(total).c_str());
  } else {
    snprintf(buf, sizeof(buf), "未掛載");
  }
  g_disp.drawText(10, y, buf, storageReady() ? COL_TEXT : COL_RED, 1);
  y += 22;

  g_disp.drawText(10, y, "人臉 ID", COL_DIM, 1);
  y += 17;
  snprintf(buf, sizeof(buf), "%d 個", faceCount());
  g_disp.drawText(10, y, buf, COL_TEXT, 1);
  y += 22;

  g_disp.drawText(10, y, "電池", COL_DIM, 1);
  y += 17;
  g_disp.drawText(10, y, powerText().c_str(), COL_TEXT, 1);
  y += 22;

  g_disp.drawText(10, y, "執行時間", COL_DIM, 1);
  y += 17;
  g_disp.drawText(10, y, uptimeText().c_str(), COL_TEXT, 1);
  g_disp.push();
}

static void screenDisplay() {
  s_brightIdx = (s_brightIdx + 1) % 4;
  uint8_t v = kBrightLevels[s_brightIdx];
  g_disp.setBrightness(v);
  g_cfg.display.brightness = v;
  g_cfg.save();

  screenFrame("顯示");
  char buf[64];
  snprintf(buf, sizeof(buf), "亮度 %u / 255", (unsigned)v);
  g_disp.drawTextCenter(96, buf, COL_TEXT, 1);
  g_disp.drawTextCenter(124, "每執行一次循環一級", COL_DIM, 1);
  g_disp.push();
}

static void screenWifiScan() {
  screenFrame("網路掃描SSID");
  g_disp.drawTextCenter(60, "掃描中…", COL_ACCENT, 1);
  g_disp.push();

  String ssids[10];
  int rssi[10];
  bool secure[10];
  int n = wifiScan(ssids, rssi, secure, 10);

  screenFrame("網路掃描SSID");
  if (n == 0) {
    g_disp.drawTextCenter(90, "找不到任何基地台", COL_RED, 1);
    g_disp.push();
    return;
  }
  int y = 40;
  for (int i = 0; i < n && i < 8; i++) {
    String s = utf8Truncate(ssids[i], 36);
    g_disp.drawText(10, y, s.c_str(), COL_TEXT, 1);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d dBm%s", rssi[i], secure[i] ? " *" : "");
    int w = g_disp.textWidth(buf, 1);
    g_disp.drawText(230 - w, y, buf, rssi[i] > -70 ? COL_GREEN : COL_DIM, 1);
    y += 20;
  }
  g_disp.drawText(10, 210, "手動連線請用網頁或序列埠", COL_DIM, 1);
  g_disp.push();
}

static void screenWifiManual() {
  screenFrame("手動輸入SSID");
  g_disp.drawTextWrapped(10, 44, 220,
                         "本機沒有鍵盤，請用下列任一方式輸入：\n\n"
                         "1. 手機連線本機 AP 後開啟 http://192.168.4.1\n"
                         "2. 以 USB 連線，序列埠輸入：\n"
                         "   wifi set <SSID> <密碼>\n\n"
                         "設定會寫回 SD 卡 config.json。",
                         COL_TEXT, COL_BG, false, 1, 20, 8);
  g_disp.push();
}

static void screenWifiClear() {
  wifiClearCredentials();
  screenFrame("清除連線設定");
  g_disp.drawTextCenter(90, "已清除 WiFi 設定", COL_GREEN, 1);
  g_disp.drawTextCenter(116, "重新開機後進入 AP 模式", COL_DIM, 1);
  g_disp.push();
}

static void screenAbout() {
  screenFrame("關於本機");
  g_disp.fillCircleAlpha(120, 62, 30, COL_ACCENT, 60);
  g_disp.fillCircle(120, 62, 18, COL_ACCENT);
  g_disp.drawTextCenter(104, "TTGO AI Camera", COL_TEXT, 1);
  g_disp.drawTextCenter(124, "T-Camera Plus", COL_DIM, 1);

  char buf[64];
  snprintf(buf, sizeof(buf), "版本 %s", APP_VERSION);
  g_disp.drawTextCenter(150, buf, COL_ACCENT, 1);
  snprintf(buf, sizeof(buf), "ESP32 核心 1.0.6");
  g_disp.drawTextCenter(170, buf, COL_DIM, 1);
  snprintf(buf, sizeof(buf), "AI：%s", g_cfg.ai.model.c_str());
  g_disp.drawTextCenter(188, buf, COL_DIM, 1);
  g_disp.drawTextCenter(210, "Google Gemini", COL_DIM, 1);
  g_disp.push();
}

// =====================================================================
//  人臉註冊 / 辨識
// =====================================================================

static String s_enrollLabel;

static void enrollTick(int sample, int total, bool faceFound, int x, int y, int w, int h) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  if (total < 1) total = 1;

  // 相機畫面每張都會覆蓋全螢幕，所以在取樣回呼中重畫標題列
  g_disp.fillRect(0, 0, 240, 30, COL_PANEL);
  g_disp.fillRect(0, 29, 240, 1, COL_ACCENT2);
  g_disp.drawText(10, 7, "加入人臉ID", COL_ACCENT, 1);
  if (!s_enrollLabel.isEmpty()) {
    String label = utf8Truncate(s_enrollLabel, 18);
    int lw = g_disp.textWidth(label.c_str(), 1);
    g_disp.drawText(230 - lw, 7, label.c_str(), COL_DIM, 1);
  }

  g_disp.fillRect(0, 196, 240, 44, COL_PANEL);
  g_disp.fillRect(10, 200, 220, 4, COL_PANEL2);
  int bw = (int)(220.0f * sample / (float)total);
  g_disp.fillRect(10, 200, bw, 4, COL_GREEN);

  char buf[48];
  snprintf(buf, sizeof(buf), "取樣 %d / %d", sample, total);
  g_disp.drawText(10, 208, buf, COL_TEXT, 1);
  g_disp.drawText(10, 224, faceFound ? "偵測到人臉" : "請面向鏡頭",
                  faceFound ? COL_GREEN : COL_RED, 1);
  g_disp.push();
}

static void screenEnroll() {
  if (!camBegin()) {
    screenMessage("加入人臉ID", "相機初始化失敗。\n\n請確認鏡頭排線，或重新開機再試。");
    return;
  }

  int idx = faceCount() + 1;
  char name[24];
  snprintf(name, sizeof(name), "ID%d", idx);
  s_enrollLabel = name;

  g_disp.clear(COL_BG);
  drawHeader("加入人臉ID");
  g_disp.drawText(10, 40, "請面向鏡頭並保持不動", COL_TEXT, 1);
  char buf[48];
  snprintf(buf, sizeof(buf), "名稱：%s", name);
  g_disp.drawText(10, 60, buf, COL_ACCENT, 1);
  g_disp.push();

  g_appBusy = true;
  String err;
  bool ok = faceEnroll(String(name), enrollTick, err);
  g_appBusy = false;

  g_disp.clear(COL_BG);
  drawHeader("加入人臉ID");
  if (ok) {
    g_disp.drawTextCenter(90, "註冊完成", COL_GREEN, 1);
    snprintf(buf, sizeof(buf), "已加入 %s（共 %d 個）", name, faceCount());
    g_disp.drawTextCenter(118, buf, COL_TEXT, 1);
  } else {
    g_disp.drawTextCenter(90, "註冊失敗", COL_RED, 1);
    g_disp.drawTextWrapped(10, 118, 220, err.c_str(), COL_TEXT, COL_BG, false, 1, 20, 4);
  }
  drawFooterHint("網頁可回到待機畫面");
  g_disp.push();
}

// 網頁觸發「加入人臉 ID」時使用：畫面與取樣進度和 TFT 內建註冊流程一致
void appEnrollBegin(const String &name) {
  s_enrollLabel = name;
  g_disp.clear(COL_BG);
  drawHeader("加入人臉ID");
  g_disp.drawText(10, 40, "請面向鏡頭並保持不動", COL_TEXT, 1);
  String line = "名稱：" + name;
  g_disp.drawText(10, 60, line.c_str(), COL_ACCENT, 1);
  g_disp.drawText(10, 86, "畫面會自動框出人臉並取樣", COL_DIM, 1);
  g_disp.push();
}

void appEnrollTick(int sample, int total, bool faceFound, int x, int y, int w, int h) {
  enrollTick(sample, total, faceFound, x, y, w, h);
}

void appEnrollEnd(bool ok, const String &msg, const String &name) {
  g_disp.clear(COL_BG);
  drawHeader("加入人臉ID");
  if (ok) {
    g_disp.drawTextCenter(90, "註冊完成", COL_GREEN, 1);
    String line = "已加入 " + name + "（共 " + String(faceCount()) + " 個）";
    g_disp.drawTextCenter(118, line.c_str(), COL_TEXT, 1);
  } else {
    g_disp.drawTextCenter(90, "註冊失敗", COL_RED, 1);
    g_disp.drawTextWrapped(10, 118, 220, msg.c_str(), COL_TEXT, COL_BG, false, 1, 20, 4);
  }
  drawFooterHint("網頁可回到待機畫面");
  g_disp.push();
}

// TFT 即時串流：QVGA 置中裁切成 240x240 連續顯示
static void screenCamStream() {
  if (!camBegin()) {
    screenMessage("串流影像", "相機初始化失敗。\n\n請確認鏡頭排線，或重新開機再試。");
    return;
  }

  g_disp.clear(COL_BG);
  g_disp.push();

  uint32_t last = 0;
  uint32_t frames = 0;
  while (s_pendingAction == 0) {
    uint32_t now = millis();
    // 相機在 core 0 取樣，這裡只負責 blit，所以可以拉到約 20 fps。
    if (now - last >= 50) {
      last = now;
      if (camPreviewToDisplay()) {
        frames++;
        g_disp.fillRect(0, 0, 74, 20, COL_PANEL);
        g_disp.drawText(6, 2, "LIVE", COL_GREEN, 1);
        g_disp.push();
      }
    }
    webuiHandle();
    delay(2);
  }
  (void)frames;
}

static void screenRecognize() {
  if (!camBegin()) {
    screenMessage("人臉辨識", "相機初始化失敗。\n\n請確認鏡頭排線，或重新開機再試。");
    return;
  }

  g_disp.clear(COL_BG);
  g_disp.push();

  uint32_t frames = 0;
  while (s_pendingAction == 0) {
    FaceResult r;
    faceDetect(r, true, true);

    g_disp.fillRect(0, 204, 240, 36, COL_PANEL);
    g_disp.fillRect(0, 204, 240, 1, COL_ACCENT2);
    if (r.detected) {
      char buf[64];
      if (r.recognized) {
        snprintf(buf, sizeof(buf), "辨識成功：%s", r.name.c_str());
        g_disp.drawText(10, 208, buf, COL_GREEN, 1);
        snprintf(buf, sizeof(buf), "相似度 %.2f", r.score);
        g_disp.drawText(10, 226, buf, COL_DIM, 1);
      } else {
        snprintf(buf, sizeof(buf), "未知人臉（相似度 %.2f）", r.score);
        g_disp.drawText(10, 208, buf, COL_RED, 1);
        g_disp.drawText(10, 226, faceCount() ? "請先到人臉設定註冊" : "尚未註冊任何人臉",
                        COL_DIM, 1);
      }
    } else {
      g_disp.drawText(10, 208, "未偵測到人臉", COL_DIM, 1);
      g_disp.drawText(10, 226, "請將臉部對準鏡頭", COL_DIM, 1);
    }
    g_disp.fillRect(0, 0, 92, 22, COL_PANEL);
    g_disp.drawText(6, 3, "人臉辨識", COL_ACCENT, 1);
    g_disp.push();

    frames++;
    (void)frames;
    webuiHandle();
    delay(10);
  }
}

static void screenFaceExport() {
  screenFrame("人臉CSV");
  String msg;
  bool ok = faceExportCsv(msg);
  g_disp.drawTextWrapped(10, 60, 220, msg.c_str(), ok ? COL_GREEN : COL_RED, COL_BG, false, 1,
                         20, 6);
  g_disp.push();
}

static void screenFaceList() {
  screenFrame("人臉ID清單");
  int n = faceCount();
  if (n == 0) {
    g_disp.drawTextCenter(90, "尚未註冊人臉", COL_DIM, 1);
    g_disp.push();
    return;
  }
  int y = 40;
  for (int i = 0; i < n && i < 8; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d. %s", i + 1, faceNameAt(i).c_str());
    g_disp.drawText(14, y, buf, COL_TEXT, 1);
    y += 20;
  }
  g_disp.push();
}

// =====================================================================
//  動作分派
// =====================================================================

static void runScreen(uint16_t action) {
  s_idle = false;
  g_appBusy = true;
  switch (action) {
    case ACT_WIFI_INFO:
      screenWifiInfo();
      break;
    case ACT_SYS_STATUS:
      screenSysStatus();
      break;
    case ACT_DISPLAY_TOGGLE:
      screenDisplay();
      break;
    case ACT_WIFI_SCAN:
      screenWifiScan();
      break;
    case ACT_WIFI_MANUAL:
      screenWifiManual();
      break;
    case ACT_WIFI_CLEAR:
      screenWifiClear();
      break;
    case ACT_AI_CHAT:
      screenChat();
      break;
    case ACT_AI_ORB:
      screenOrb();
      break;
    case ACT_AI_CLEAR:
      appClearHistory();
      screenMessage("AI聊天機器人", "已清除本機對話記錄。");
      break;
    case ACT_FACE_ENROLL:
      screenEnroll();
      break;
    case ACT_FACE_EXPORT:
      screenFaceExport();
      break;
    case ACT_FACE_RECOGNIZE:
      screenRecognize();
      break;
    case ACT_CAM_STREAM:
      screenCamStream();
      break;
    case ACT_FACE_LIST:
      screenFaceList();
      break;
    case ACT_ABOUT:
      screenAbout();
      break;
    case ACT_REBOOT:
      screenMessage("重新開機", "即將重新啟動…");
      delay(400);
      ESP.restart();
      break;
    default:
      break;
  }
  g_appBusy = false;
}

// =====================================================================
//  對外 API（給網頁 / 序列埠）
// =====================================================================

void appRunAction(uint16_t action) { s_pendingAction = action; }

bool appCanRunAction() { return s_pendingAction == 0 && !g_appBusy; }

void appAddHistory(const String &user, const String &reply) {
  s_turns[s_turnNext].user = user;
  s_turns[s_turnNext].reply = reply;
  s_turnNext = (s_turnNext + 1) % HIST_TURNS;
  if (s_turnCount < HIST_TURNS) s_turnCount++;
}

void appClearHistory() {
  for (int i = 0; i < HIST_TURNS; i++) {
    s_turns[i].user = "";
    s_turns[i].reply = "";
  }
  s_turnCount = 0;
  s_turnNext = 0;
}

String appHistoryJson() {
  String s = "{\"turns\":[";
  int start = (s_turnNext - s_turnCount + HIST_TURNS) % HIST_TURNS;
  for (int i = 0; i < s_turnCount; i++) {
    int idx = (start + i) % HIST_TURNS;
    if (i) s += ",";
    s += "{\"user\":\"" + jsonEscape(s_turns[idx].user) + "\",\"reply\":\"" +
         jsonEscape(s_turns[idx].reply) + "\"}";
  }
  s += "]}";
  return s;
}

String appStatusJson() {
  uint64_t total = 0, used = 0;
  storageInfo(total, used);

  String s;
  s.reserve(768);
  s += "{\"wifi\":{\"ssid\":\"" + jsonEscape(g_cfg.wifi.ssid) + "\",\"ip\":\"" +
       jsonEscape(wifiConnected() ? wifiIP() : String()) + "\",\"state\":\"" +
       jsonEscape(wifiStatusText()) + "\",\"rssi\":" + String(wifiConnected() ? wifiRSSI() : 0) +
       ",\"ap\":\"" + jsonEscape(wifiApSsid()) + "\",\"apActive\":" +
       (wifiApActive() ? "true" : "false") + "},";

  s += "\"sys\":{\"heap\":" + String((unsigned)(ESP.getFreeHeap() / 1024)) +
       ",\"psram\":\"" + jsonEscape(humanBytes(ESP.getFreePsram())) + "\",\"sd\":\"" +
       jsonEscape(storageReady() ? humanBytes(used) + " / " + humanBytes(total) : String("未掛載")) +
       "\",\"uptime\":\"" + jsonEscape(uptimeText()) + "\",\"font\":" +
       (g_font.ok() ? "true" : "false") + "},";

  s += "\"faces\":" + String(faceCount());
  s += ",\"cam\":{\"ready\":" + String(camReady() ? "true" : "false") +
       ",\"fps\":" + String((unsigned)camFps()) +
       ",\"bytes\":" + String((unsigned)camLastFrameBytes()) +
       ",\"errors\":" + String((unsigned)camErrorCount()) + "}";
  s += ",\"power\":\"" + jsonEscape(powerText()) + "\"";
  s += ",\"ai\":" + String(geminiReady() ? "true" : "false");
  s += ",\"model\":\"" + jsonEscape(g_cfg.ai.model) + "\"";
  s += ",\"screen\":\"" + String(s_idle ? "idle" : "active") + "\"";
  s += "}";
  return s;
}

// =====================================================================
//  Gemini 任務
// =====================================================================

struct AiJob {
  bool audio;
  String text;
  const uint8_t *wav;
  size_t wavLen;
  AiReply reply;
  volatile int state;  // 1 = 執行中, 2 = 完成
};

static AiJob *s_job = nullptr;

static void aiTaskFn(void *arg) {
  AiJob *j = (AiJob *)arg;
  if (j->audio) {
    geminiChatAudio(j->wav, j->wavLen, j->reply);
  } else {
    geminiChatText(j->text, j->reply);
  }
  __sync_synchronize();
  j->state = 2;
  vTaskDelete(NULL);
}

bool appAiRun(bool audio, const String &text, const uint8_t *wav, size_t wavLen, AiReply &out,
              AiTickFn tick) {
  if (s_job) {
    out.error = "上一個 AI 請求還在進行中";
    return false;
  }

  AiJob *j = new AiJob();
  if (!j) {
    out.error = "記憶體不足";
    return false;
  }
  j->audio = audio;
  j->text = text;
  j->wav = wav;
  j->wavLen = wavLen;
  j->state = 1;
  s_job = j;

  // AI 與 TFT / 網頁一起留在 core 1；core 0 專門跑相機取樣。
  BaseType_t ok = xTaskCreatePinnedToCore(aiTaskFn, "gemini", 16384, j, 1, NULL, 1);
  if (ok != pdPASS) {
    s_job = nullptr;
    delete j;
    out.error = "無法建立 AI 工作任務";
    return false;
  }

  while (j->state != 2) {
    if (tick) tick();
    delay(30);
  }

  __sync_synchronize();
  out = j->reply;
  s_job = nullptr;
  delete j;
  return out.ok;
}

// =====================================================================
//  setup / loop
// =====================================================================

static void bootSplash() {
  g_disp.clear(COL_BG);
  g_disp.fillCircleAlpha(120, 92, 56, COL_ACCENT, 24);
  g_disp.fillCircleAlpha(120, 92, 40, COL_ACCENT, 40);
  g_disp.fillCircle(120, 92, 26, COL_ACCENT);
  g_disp.fillCircleAlpha(110, 82, 10, COL_WHITE, 110);
  g_disp.drawTextCenter(154, "TTGO AI Camera", COL_TEXT, 1);
  g_disp.drawTextCenter(174, "T-Camera Plus", COL_DIM, 1);
  g_disp.push();
}

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println("=================================");
  Serial.println(" TTGO T-Camera Plus - AI Camera");
  Serial.println("=================================");

  storageBegin();
  g_cfg.load();

  // 開機記憶體診斷：螢幕緩衝區與字型都依賴 PSRAM
  if (!psramFound()) {
    Serial.println("[SYS] PSRAM 未啟用！請在 Tools → PSRAM 選「Enabled」後重新燒錄，");
    Serial.println("[SYS] 否則顯示緩衝區（115 KB）與字型（593 KB）都無法配置。");
  }
  Serial.printf("[SYS] free heap %u bytes / free PSRAM %u bytes\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());

  if (!g_disp.begin()) {
    Serial.println("[LCD] 初始化失敗");
  }
  g_disp.setBrightness((uint8_t)g_cfg.display.brightness);

  if (!g_font.begin(g_cfg.font_path)) {
    Serial.println("[FONT] 使用內建 5x7 備援字型");
  }

  bootSplash();
  delay(500);

  powerBegin();
  faceLoad();

  wifiBegin();
  webuiBegin();

  appShowIdle();
  Serial.println("[SYS] core 0 = 相機取樣／core 1 = AI、TFT、網頁");
  Serial.println("[LCD] 待機畫面已顯示（所有操作請由網頁進行）");
}

void loop() {
  webuiHandle();
  wifiLoop();

  if (s_pendingAction != 0 && !g_appBusy) {
    uint16_t action = s_pendingAction;
    s_pendingAction = 0;
    if (action == ACT_BACK) {
      appShowIdle();
    } else {
      runScreen(action);
      if (s_pendingAction == 0) {
        // 會自己持續更新的畫面已用 pending 離開，不必再等；其餘結果畫面停留 4 秒。
        if (action != ACT_CAM_STREAM && action != ACT_AI_CHAT && action != ACT_AI_ORB &&
            action != ACT_FACE_RECOGNIZE) {
          holdScreen(4000);
        }
        if (s_pendingAction == 0) appShowIdle();
      }
    }
  } else {
    appIdleTick();
  }

  delay(5);
}
