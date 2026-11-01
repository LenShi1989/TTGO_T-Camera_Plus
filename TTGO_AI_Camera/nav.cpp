#include "nav.h"

#include <string.h>

RTC_DATA_ATTR static NavState s_nav;

bool navWarmBoot() {
  if (s_nav.magic != NAV_MAGIC) {
    navReset();
    return false;
  }
  s_nav.bootCount++;
  return true;
}

NavState &nav() { return s_nav; }

void navPersist() { s_nav.magic = NAV_MAGIC; }

void navSetMode(uint8_t mode) {
  s_nav.mode = mode;
  navPersist();
}

void navSetCursor(uint8_t depth, uint8_t cursor) {
  if (depth < NAV_MAX_DEPTH) s_nav.cursor[depth] = cursor;
  navPersist();
}

void navReset() {
  memset(&s_nav, 0, sizeof(s_nav));
  s_nav.magic = NAV_MAGIC;
  s_nav.mode = MODE_MENU;
  s_nav.depth = 0;
  s_nav.bootCount = 1;
}

const char *navModeName(uint8_t mode) {
  switch (mode) {
    case MODE_MENU:
      return "選單";
    case MODE_CHAT:
      return "AI 聊天";
    case MODE_CHART:
      return "環境圖表";
    case MODE_ENROLL:
      return "人臉註冊";
    case MODE_RECOGNIZE:
      return "人臉辨識";
    case MODE_INFO:
      return "系統資訊";
    case MODE_TEXT:
      return "文字模式";
    default:
      return "未知";
  }
}
