#pragma once
#include <Arduino.h>

#include "esp_camera.h"

struct FaceResult {
  bool detected = false;
  bool recognized = false;
  float score = 0;
  String name;
  int x = 0, y = 0, w = 0, h = 0; // 螢幕座標
};

typedef void (*EnrollProgressFn)(int sample, int total, bool faceFound, int x, int y, int w, int h);

bool camBegin();
bool camReady();
void camEnd();

// 擷取一張畫面並顯示在 TFT（QVGA 置中裁切為 240x240）
bool camPreviewToDisplay();

// 擷取一張並轉成 JPEG（由呼叫端 free）
bool camSnapshotJpeg(uint8_t **out, size_t *outLen);

// 零複製取樣：向 core 0 的相機任務借一張影格，用完務必呼叫 camReturnFrame()。
// 網頁即時影像走這條路，省掉每張 JPEG 的 ps_malloc + memcpy。
// timeoutMs = 0 表示不等；相機比消費者慢時會回傳 nullptr。
camera_fb_t *camGrabFrame(uint32_t timeoutMs = 800);
void camReturnFrame(camera_fb_t *fb);

// 串流統計（給 /api/status 與序列埠診斷用）
uint32_t camFps();
uint32_t camLastFrameBytes();
uint32_t camErrorCount();

// ---- 人臉資料 ----
int faceCount();
String faceNameAt(int idx);
bool faceLoad();
bool faceSave();
bool faceExportCsv(String &msg);
bool faceRemoveAt(int idx);
bool faceRemove(const String &name);
bool faceRename(int idx, const String &newName);

// 連續取樣註冊；name 為顯示名稱
bool faceEnroll(const String &name, EnrollProgressFn cb, String &err);

// 偵測（可同時辨識）並把結果框在螢幕上
bool faceDetect(FaceResult &res, bool doRecognize, bool drawBox);
