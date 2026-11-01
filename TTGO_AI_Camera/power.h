#pragma once
#include <Arduino.h>

struct PowerInfo {
  bool ok = false;
  int percent = -1; // -1 表示未知
  bool charging = false;
  bool full = false;
};

bool powerRead(PowerInfo &out);
String powerText();
