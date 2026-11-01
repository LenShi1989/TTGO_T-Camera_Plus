#pragma once
#include <Arduino.h>

struct BmeReading {
  bool valid = false;
  float temperature = 0; // °C
  float pressure = 0;    // hPa
  float humidity = 0;    // %RH
  uint32_t stamp = 0;
};

bool bmeBegin();          // 自動偵測 0x76 / 0x77
bool bmePresent();
uint8_t bmeAddress();
bool bmeRead(BmeReading &out);
