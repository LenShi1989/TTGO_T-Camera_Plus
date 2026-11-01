#pragma once
#include <Arduino.h>

// 這塊板子只有 RST（接到 EN）可用，所以用 RTC 記憶體保存選單狀態：
//   每次按下 RST 都會重新開機，開機後游標前進一格，停在該項目 2.5 秒即自動執行。
enum NavMode : uint8_t {
  MODE_MENU = 0,
  MODE_CHAT = 1,
  MODE_CHART = 2,
  MODE_ENROLL = 3,
  MODE_RECOGNIZE = 4,
  MODE_INFO = 5,
  MODE_TEXT = 6,
};

#define NAV_MAGIC 0x4E415632UL // "NAV2"
#define NAV_MAX_DEPTH 4

struct NavState {
  uint32_t magic;
  uint8_t mode;
  uint8_t depth;
  uint8_t cursor[NAV_MAX_DEPTH];
  uint8_t dynActive;
  uint8_t dynCount;
  uint8_t hold;  // 1 = 顯示選單但先不啟動倒數（等下一次 RST）
  uint8_t pad;
  uint32_t bootCount;
};

// 回傳 true 表示這次是「按 RST 熱開機」，狀態已從 RTC 記憶體還原
bool navWarmBoot();
NavState &nav();
void navPersist();
void navSetMode(uint8_t mode);
void navSetCursor(uint8_t depth, uint8_t cursor);
void navReset();

const char *navModeName(uint8_t mode);
