#pragma once
#include <Arduino.h>

enum AppAction : uint16_t {
  ACT_NONE = 0,
  ACT_BACK = 2,
  ACT_WIFI_INFO = 10,
  ACT_SYS_STATUS = 11,
  ACT_DISPLAY_TOGGLE = 12,
  ACT_WIFI_SCAN = 20,
  ACT_WIFI_MANUAL = 21,
  ACT_WIFI_CLEAR = 22,
  ACT_AI_CHAT = 30,
  ACT_AI_ORB = 31,
  ACT_AI_CLEAR = 32,
  ACT_FACE_ENROLL = 50,
  ACT_FACE_DELETE = 51,
  ACT_FACE_EXPORT = 52,
  ACT_FACE_RECOGNIZE = 53,
  ACT_FACE_LIST = 54,
  ACT_FACE_DELETE_ONE = 55,
  ACT_CAM_STREAM = 56,
  ACT_ABOUT = 60,
  ACT_REBOOT = 61,
};

// 由 TTGO_AI_Camera.ino 提供
void appRunAction(uint16_t action);
void appShowIdle();  // 顯示 TFT 待機儀表板
void appIdleTick();  // 待機時每 5 秒更新儀表板
String appStatusJson();
String appHistoryJson();
void appClearHistory();
void appAddHistory(const String &user, const String &reply);

// 主程式是否正在處理長時間動作（錄音、辨識、繪圖…）
extern volatile bool g_appBusy;
// 目前是否允許從網頁/序列埠觸發動作
bool appCanRunAction();

// ---- Gemini 呼叫（一律在獨立任務執行，避免 TLS 握手撐爆 loop 堆疊）----
struct AiReply;  // 定義於 gemini.h
typedef void (*AiTickFn)(void);

// 同步等待結果；tick 可為 nullptr，等待期間每 ~30ms 呼叫一次
bool appAiRun(bool audio, const String &text, const uint8_t *wav, size_t wavLen, AiReply &out,
              AiTickFn tick);

// 網頁語音 / 文字請求期間，讓 TFT 顯示球體動畫（不必切換到本機畫面）
void appOrbThinking();
void appOrbTick();

// 網頁「加入人臉 ID」時，同步在 TFT 顯示鏡頭畫面、綠框與取樣進度
void appEnrollBegin(const String &name);
void appEnrollTick(int sample, int total, bool faceFound, int x, int y, int w, int h);
void appEnrollEnd(bool ok, const String &msg, const String &name);
