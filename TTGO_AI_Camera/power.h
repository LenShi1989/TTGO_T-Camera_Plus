#pragma once
#include <Arduino.h>

struct PowerInfo {
  bool ok = false;
  int percent = -1; // -1 表示未知
  bool charging = false;
  bool full = false;
};

void powerBegin(); // 初始化 I2C1（SDA 18 / SCL 23）
bool powerRead(PowerInfo &out);
String powerText();
