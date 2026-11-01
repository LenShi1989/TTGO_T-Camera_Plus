// =====================================================================
//  TTGO T-Camera Plus — AI 相機助理
//  ESP32 / ST7789 240x240 / OV2640 / I2S 麥克風 / microSD / BME280
//
//  操作方式：本板沒有使用者按鍵，RST 直接接 EN。
//    - 冷開機：顯示主選單，等待第一次 RST。
//    - 每按一次 RST：游標往下移動一格（到底繞回第一項）。
//    - 游標停在某項 2.5 秒（可在 config.json 調整）即自動執行。
//    - 在子畫面按 RST：回到選單並自動移到下一項。
//  另外提供網頁介面與序列埠指令，方便直接操作。
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
#include "bme280.h"
#include "board_pins.h"
#include "camera_face.h"
#include "display.h"
#include "font.h"
#include "gemini.h"
#include "nav.h"
#include "power.h"
#include "serial_console.h"
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

static bool s_countdownActive = false;
static uint32_t s_countdownStart = 0;
static uint32_t s_countdownEnd = 0;

static int s_lastCountdownW = -1;

// ---- 對話記錄 ----
#define HIST_TURNS 10
struct ChatTurn {
  String user;
  String reply;
};
static ChatTurn s_turns[HIST_TURNS];
static int s_turnCount = 0;
static int s_turnNext = 0;

// ---- 環境序列 ----
#define ENV_MAX 120
static float s_envT[ENV_MAX];
static float s_envH[ENV_MAX];
static float s_envP[ENV_MAX];
static int s_envN = 0;
static int s_envHead = 0;

// ---- 顯示亮度循環 ----
static const uint8_t kBrightLevels[] = {255, 170, 100, 40};
static int s_brightIdx = 0;

// ---- AI 回覆分頁 ----
#define REPLY_LINES 12
static String s_replyLines[REPLY_LINES];
static int s_replyLineCount = 0;
static int s_replyPage = 0;

// ---- 動態人臉刪除選單 ----
#define DYN_MAX 20
static char s_dynLabels[DYN_MAX][32];
static MenuItem s_dynItems[DYN_MAX];
static int s_dynCount = 0;

// =====================================================================
//  選單資料
// =====================================================================

static const MenuItem kMainMenu[] = {
    {"系統狀態", ACT_SUBMENU, "WiFi / 狀態 / 顯示"},
    {"WiFi設定", ACT_SUBMENU, "掃描 / 手動 / 清除"},
    {"AI聊天機器人", ACT_SUBMENU, "語音對話 / 球體動畫"},
    {"環境參數", ACT_BME_CHART, "BME280 折線圖"},
    {"CAMERA", ACT_SUBMENU, "人臉設定 / 人臉辨識"},
    {"關於本機", ACT_ABOUT, "版本與記憶體資訊"},
};
static const int kMainCount = 6;

static const MenuItem kSysMenu[] = {
    {"WiFi連線資訊", ACT_WIFI_INFO, "SSID / IP / 訊號"},
    {"系統狀態", ACT_SYS_STATUS, "記憶體 / SD / 電池"},
    {"顯示", ACT_DISPLAY_TOGGLE, "切換螢幕亮度"},
    {"返回", ACT_BACK, "回上一層"},
};

static const MenuItem kWifiMenu[] = {
    {"網路掃描SSID", ACT_WIFI_SCAN, "列出附近基地台"},
    {"手動輸入SSID+密碼", ACT_WIFI_MANUAL, "由網頁或序列埠輸入"},
    {"清除連線設定", ACT_WIFI_CLEAR, "清除後改用 AP 模式"},
    {"返回", ACT_BACK, "回上一層"},
};

static const MenuItem kAiMenu[] = {
    {"麥克風收音", ACT_AI_CHAT, "錄音並與 Gemini 對話"},
    {"TFT OLED顯示", ACT_AI_ORB, "球體動畫 / 最近回覆"},
    {"清除對話記錄", ACT_AI_CLEAR, "清空本機歷史"},
    {"返回", ACT_BACK, "回上一層"},
};

static const MenuItem kCamMenu[] = {
    {"人臉設定", ACT_SUBMENU, "加入 / 刪除 / 匯出"},
    {"人臉辨識", ACT_FACE_RECOGNIZE, "綠框成功 / 紅框失敗"},
    {"返回", ACT_BACK, "回上一層"},
};

static const MenuItem kFaceMenu[] = {
    {"加入人臉ID", ACT_FACE_ENROLL, "面向鏡頭約 3 秒"},
    {"刪除人臉ID", ACT_FACE_DELETE, "選擇要刪除的 ID"},
    {"人臉ID參數CSV存到SD卡", ACT_FACE_EXPORT, "寫入 /faces.csv"},
    {"返回", ACT_BACK, "回上一層"},
};

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
//  選單結構
// =====================================================================

static int menuCount() {
  NavState &n = nav();
  if (n.depth == 3) return n.dynActive ? (int)n.dynCount : 0;
  if (n.depth == 0) return kMainCount;
  if (n.depth == 1) {
    switch (n.cursor[0]) {
      case 0: return 4;
      case 1: return 4;
      case 2: return 4;
      case 4: return 3;
      default: return 0;
    }
  }
  if (n.depth == 2) return 4;
  return 0;
}

static MenuItem menuItemAt(int idx) {
  NavState &n = nav();
  MenuItem none = {"", ACT_NONE, nullptr};
  if (idx < 0) return none;

  if (n.depth == 3) {
    if (n.dynActive && idx < n.dynCount && idx < DYN_MAX) return s_dynItems[idx];
    return none;
  }
  if (n.depth == 0) {
    if (idx < kMainCount) return kMainMenu[idx];
    return none;
  }
  if (n.depth == 1) {
    switch (n.cursor[0]) {
      case 0:
        if (idx < 4) return kSysMenu[idx];
        break;
      case 1:
        if (idx < 4) return kWifiMenu[idx];
        break;
      case 2:
        if (idx < 4) return kAiMenu[idx];
        break;
      case 4:
        if (idx < 3) return kCamMenu[idx];
        break;
      default:
        break;
    }
    return none;
  }
  if (n.depth == 2) {
    if (idx < 4) return kFaceMenu[idx];
    return none;
  }
  return none;
}

static String menuTitle() {
  NavState &n = nav();
  if (n.depth == 0) return "主選單";
  if (n.depth == 1) {
    int p = n.cursor[0];
    if (p >= 0 && p < kMainCount) return String(kMainMenu[p].label);
    return "選單";
  }
  if (n.depth == 2) return "人臉設定";
  if (n.depth == 3) return "刪除人臉ID";
  return "選單";
}

static void buildFaceDeleteMenu() {
  int n = faceCount();
  if (n > DYN_MAX - 1) n = DYN_MAX - 1;
  for (int i = 0; i < n; i++) {
    String nm = faceNameAt(i);
    snprintf(s_dynLabels[i], sizeof(s_dynLabels[i]), "%d. %s", i + 1, nm.c_str());
    s_dynItems[i].label = s_dynLabels[i];
    s_dynItems[i].action = ACT_FACE_DELETE_ONE;
    s_dynItems[i].hint = "刪除";
  }
  s_dynItems[n].label = "返回";
  s_dynItems[n].action = ACT_BACK;
  s_dynItems[n].hint = "回上一層";
  s_dynCount = n + 1;

  NavState &st = nav();
  st.dynActive = 1;
  st.dynCount = (uint8_t)s_dynCount;
  if (st.cursor[3] != 255 && st.cursor[3] >= s_dynCount) st.cursor[3] = 255;
}

static void menuAdvance() {
  NavState &n = nav();
  if (n.depth >= NAV_MAX_DEPTH) return;
  int cnt = menuCount();
  if (cnt <= 0) return;
  uint8_t &c = n.cursor[n.depth];
  if (c == 255) {
    c = 0;
  } else {
    c = (uint8_t)((c + 1) % cnt);
  }
}

// =====================================================================
//  選單畫面
// =====================================================================

static void renderMenu() {
  NavState &n = nav();
  g_disp.clear(COL_BG);
  drawHeader(menuTitle().c_str());

  int cnt = menuCount();
  int cur = (n.depth < NAV_MAX_DEPTH) ? n.cursor[n.depth] : 255;
  int visible = cnt > 6 ? 6 : cnt;

  int first = 0;
  if (cnt > 6 && cur != 255) {
    first = (int)cur - 2;
    if (first < 0) first = 0;
    if (first > cnt - 6) first = cnt - 6;
  }

  for (int k = 0; k < visible; k++) {
    int i = first + k;
    MenuItem it = menuItemAt(i);
    int iy = 36 + k * 30;
    bool sel = (i == (int)cur);
    if (sel) {
      g_disp.fillRoundRect(8, iy, 224, 26, 6, COL_PANEL2);
      g_disp.fillRect(8, iy, 3, 26, COL_ACCENT);
    }
    g_disp.drawText(20, iy + 5, it.label, sel ? COL_WHITE : COL_TEXT, 1);
    if (sel && it.hint) {
      int w = g_disp.textWidth(it.hint, 1);
      g_disp.drawText(226 - w, iy + 5, it.hint, COL_DIM, 1);
    }
  }

  if (cnt == 0) {
    g_disp.drawTextCenter(100, "（沒有項目）", COL_DIM, 1);
  }

  // 頁碼
  if (cnt > 6) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%d/%d", (int)cur + 1, cnt);
    g_disp.drawText(210, 7, buf, COL_DIM, 1);
  }

  drawFooterHint(cur == 255 ? "按 RST 選擇下一項" : "倒數結束後自動執行");

  g_disp.push();
}

static void updateCountdown(uint32_t now) {
  uint32_t total = g_cfg.display.nav_timeout_ms;
  if (total < 400) total = 400;
  uint32_t el = now - s_countdownStart;
  if (el > total) el = total;
  int w = (int)((uint64_t)el * 240ULL / total);
  if (w == s_lastCountdownW) return;
  s_lastCountdownW = w;
  g_disp.fillRect(0, 236, 240, 4, COL_PANEL2);
  g_disp.fillRect(0, 236, w, 4, COL_ACCENT);
  g_disp.pushRect(0, 236, 240, 4);
}

void appShowMenu() {
  nav().mode = MODE_MENU;
  renderMenu();
  NavState &n = nav();
  if (!n.hold && n.depth < NAV_MAX_DEPTH && n.cursor[n.depth] != 255) {
    s_countdownStart = millis();
    s_countdownEnd = s_countdownStart + g_cfg.display.nav_timeout_ms;
    s_countdownActive = true;
    s_lastCountdownW = -1;
    updateCountdown(s_countdownStart);
  } else {
    s_countdownActive = false;
    s_lastCountdownW = -1;
  }
}

static void returnToMenu() {
  nav().mode = MODE_MENU;
  nav().hold = 0;
  menuAdvance();
  navPersist();
  appShowMenu();
}

// =====================================================================
//  共用畫面元件
// =====================================================================

static void screenFrame(const char *title) {
  g_disp.clear(COL_BG);
  drawHeader(title);
  drawFooterHint("按 RST 返回選單");
}

static void screenMessage(const char *title, const char *body) {
  screenFrame(title);
  g_disp.drawTextWrapped(10, 44, 220, body, COL_TEXT, COL_BG, false, 1, 20, 8);
  g_disp.push();
}

static void pushAndWait(uint32_t ms) {
  g_disp.push();
  uint32_t start = millis();
  while (millis() - start < ms) {
    webuiHandle();
    serialPoll();
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

static void chatAiTick() {
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
    serialPoll();
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
                  "尚未設定 Gemini API Key。\n\n請編輯 SD 卡 /config.json 的 ai.api_key，"
                  "或用手機連線到本機 AP 後開啟網頁設定。");
    return;
  }
  if (!wifiConnected()) {
    screenMessage("AI聊天機器人",
                  "WiFi 尚未連線。\n\n請先進入「WiFi設定」完成連線，"
                  "或用手機連線本機 AP 後由網頁設定。");
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
    chatIdleLoop("發生錯誤，按 RST 返回");
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
  chatIdleLoop("按 RST 返回選單");
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
  chatIdleLoop("按 RST 返回選單");
}

// =====================================================================
//  環境圖表
// =====================================================================

static void envPush(const BmeReading &r) {
  s_envT[s_envHead] = r.temperature;
  s_envH[s_envHead] = r.humidity;
  s_envP[s_envHead] = r.pressure;
  s_envHead = (s_envHead + 1) % ENV_MAX;
  if (s_envN < ENV_MAX) s_envN++;
}

static int envIndex(int i) {
  // i = 0 為最舊
  int start = (s_envHead - s_envN + ENV_MAX) % ENV_MAX;
  return (start + i) % ENV_MAX;
}

static void drawChartRow(int y, const char *label, const char *value, const char *unit,
                         const float *data, float minV, float maxV, uint16_t color) {
  g_disp.fillRect(8, y, 224, 54, COL_PANEL);
  g_disp.drawText(14, y + 4, label, COL_DIM, 1);
  g_disp.drawText(14, y + 22, value, color, 1);
  int vw = g_disp.textWidth(value, 1);
  g_disp.drawText(14 + vw + 4, y + 26, unit, COL_DIM, 1);

  int cx0 = 78, cx1 = 226;
  int cy0 = y + 8, cy1 = y + 46;
  g_disp.drawHLine(cx0, cy1, cx1 - cx0, COL_PANEL2);
  g_disp.drawHLine(cx0, (cy0 + cy1) / 2, cx1 - cx0, COL_PANEL2);

  if (s_envN < 2) return;
  int denom = s_envN > 1 ? s_envN - 1 : 1;
  int px = -1, py = -1;
  for (int i = 0; i < s_envN; i++) {
    float v = data[envIndex(i)];
    float t = (v - minV) / (maxV - minV);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int x = cx0 + (int)((float)(cx1 - cx0) * i / (float)denom);
    int yy = cy1 - (int)((cy1 - cy0) * t);
    if (px >= 0) g_disp.drawLine(px, py, x, yy, color);
    px = x;
    py = yy;
  }
}

static void screenChart() {
  g_disp.clear(COL_BG);
  drawHeader("環境參數");
  drawFooterHint("每 2 秒更新，按 RST 返回");
  g_disp.push();

  if (!bmePresent()) {
    screenMessage("環境參數", "未偵測到 BME280。\n\n請確認感測器接在 I2C（SDA 18 / SCL 23），"
                              "或在 config.json 設定 bme280.enabled。");
    return;
  }

  s_envN = 0;
  s_envHead = 0;

  uint32_t lastRead = 0;
  while (s_pendingAction == 0) {
    uint32_t now = millis();
    if (now - lastRead >= g_cfg.bme.interval_ms) {
      lastRead = now;
      BmeReading r;
      if (bmeRead(r)) {
        envPush(r);
        char tb[24], hb[24], pb[24];
        snprintf(tb, sizeof(tb), "%.1f", r.temperature);
        snprintf(hb, sizeof(hb), "%.1f", r.humidity);
        snprintf(pb, sizeof(pb), "%.1f", r.pressure);
        drawChartRow(36, "溫度", tb, "°C", s_envT, 0.0f, 50.0f, COL_RED);
        drawChartRow(94, "濕度", hb, "%", s_envH, 0.0f, 100.0f, COL_CYAN);
        drawChartRow(152, "氣壓", pb, "hPa", s_envP, 950.0f, 1050.0f, COL_GREEN);
        g_disp.pushRect(0, 36, 240, 170);
      }
    }
    webuiHandle();
    serialPoll();
    delay(20);
  }
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

  g_disp.drawText(10, y, "BME280", COL_DIM, 1);
  y += 17;
  g_disp.drawText(10, y, bmePresent() ? "已連接" : "未偵測", bmePresent() ? COL_GREEN : COL_RED, 1);
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

static void enrollTick(int sample, int total, bool faceFound, int x, int y, int w, int h) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  if (total < 1) total = 1;

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
  drawFooterHint("按 RST 返回選單");
  g_disp.push();
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
    serialPoll();
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
    case ACT_BME_CHART:
      screenChart();
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

static void enterSubmenu() {
  NavState &n = nav();
  if (n.depth + 1 < NAV_MAX_DEPTH) {
    n.depth++;
    n.cursor[n.depth] = 255;
  }
  n.hold = 1;
  navPersist();
  appShowMenu();
}

static void goBack() {
  NavState &n = nav();
  if (n.depth > 0) n.depth--;
  if (n.depth < 3) n.dynActive = 0;
  n.hold = 1;
  navPersist();
  appShowMenu();
}

static void executeCurrent() {
  NavState &n = nav();
  int cnt = menuCount();
  if (cnt <= 0) {
    n.hold = 1;
    navPersist();
    appShowMenu();
    return;
  }
  if (n.cursor[n.depth] == 255 || n.cursor[n.depth] >= cnt) {
    n.hold = 1;
    navPersist();
    appShowMenu();
    return;
  }

  MenuItem it = menuItemAt(n.cursor[n.depth]);

  if (it.action == ACT_SUBMENU) {
    enterSubmenu();
    return;
  }
  if (it.action == ACT_BACK) {
    goBack();
    return;
  }
  if (it.action == ACT_NONE) {
    n.hold = 1;
    navPersist();
    appShowMenu();
    return;
  }

  if (it.action == ACT_FACE_DELETE) {
    buildFaceDeleteMenu();
    n.depth = 3;
    n.cursor[3] = 255;
    n.hold = 1;
    navPersist();
    appShowMenu();
    return;
  }

  if (it.action == ACT_FACE_DELETE_ONE) {
    int idx = n.cursor[3];
    String nm = faceNameAt(idx);
    bool ok = faceRemoveAt(idx);
    n.depth = 2;
    n.cursor[2] = 255;
    n.dynActive = 0;
    n.hold = 1;
    navPersist();

    screenFrame("刪除人臉ID");
    if (ok) {
      g_disp.drawTextCenter(90, "已刪除", COL_GREEN, 1);
      g_disp.drawTextCenter(116, nm.c_str(), COL_TEXT, 1);
    } else {
      g_disp.drawTextCenter(90, "刪除失敗", COL_RED, 1);
    }
    g_disp.push();
    return;
  }

  runScreen(it.action);
  returnToMenu();
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
  s.reserve(640);
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
  s += ",\"bme\":" + String(bmePresent() ? "true" : "false");
  s += ",\"power\":\"" + jsonEscape(powerText()) + "\"";
  s += ",\"ai\":" + String(geminiReady() ? "true" : "false");
  s += ",\"model\":\"" + jsonEscape(g_cfg.ai.model) + "\"";
  s += ",\"nav\":{\"mode\":" + String((int)nav().mode) + ",\"depth\":" +
       String((int)nav().depth) + "}";
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

  BaseType_t ok = xTaskCreatePinnedToCore(aiTaskFn, "gemini", 16384, j, 1, NULL, 0);
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

  bool warm = navWarmBoot();
  NavState &n = nav();

  storageBegin();
  g_cfg.load();

  if (!g_disp.begin()) {
    Serial.println("[LCD] 初始化失敗");
  }
  g_disp.setBrightness((uint8_t)g_cfg.display.brightness);

  if (!g_font.begin(g_cfg.font_path)) {
    Serial.println("[FONT] 使用內建 5x7 備援字型");
  }

  bootSplash();
  delay(500);

  bmeBegin();
  faceLoad();

  wifiBegin();
  webuiBegin();
  serialBegin();

  // 導覽狀態
  if (warm) {
    if (n.mode != MODE_MENU) {
      // 從子畫面按 RST：回選單並前進一格
      n.mode = MODE_MENU;
      n.hold = 0;
      menuAdvance();
    } else if (n.hold) {
      // 剛進入子選單 / 剛返回：游標不動，直接倒數
      n.hold = 0;
    } else {
      menuAdvance();
    }
    if (n.depth < NAV_MAX_DEPTH && n.cursor[n.depth] == 255) n.cursor[n.depth] = 0;
  } else {
    n.mode = MODE_MENU;
    n.depth = 0;
    for (int i = 0; i < NAV_MAX_DEPTH; i++) n.cursor[i] = 255;
    n.dynActive = 0;
    n.dynCount = 0;
    n.hold = 1;
    Serial.println("[NAV] 冷開機，等待第一次 RST");
  }

  if (n.depth == 3) buildFaceDeleteMenu();
  navPersist();

  appShowMenu();
  Serial.println("[NAV] 選單已顯示");
}

void loop() {
  serialPoll();
  webuiHandle();
  wifiLoop();

  if (s_countdownActive) {
    uint32_t now = millis();
    if ((int32_t)(now - s_countdownEnd) >= 0) {
      s_countdownActive = false;
      executeCurrent();
    } else {
      updateCountdown(now);
    }
  }

  if (s_pendingAction != 0 && !g_appBusy) {
    uint16_t action = s_pendingAction;
    s_pendingAction = 0;
    if (action == ACT_FACE_DELETE) {
      buildFaceDeleteMenu();
      nav().depth = 3;
      nav().cursor[3] = 255;
      nav().hold = 1;
      navPersist();
      appShowMenu();
    } else {
      runScreen(action);
      nav().hold = 1;
      navPersist();
      appShowMenu();
    }
  }

  delay(5);
}
