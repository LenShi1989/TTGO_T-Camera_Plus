#pragma once
#include <Arduino.h>

struct BmeReading {
  bool valid = false;
  float temperature = 0; // °C
  float pressure = 0;    // hPa
  float humidity = 0;    // %RH（BMP280 時固定為 0）
  uint32_t stamp = 0;
};

bool bmeBegin();          // 自動偵測 0x76 / 0x77（BME280 / BMP280）
bool bmeEnsure();         // 尚未就緒時，每 3 秒嘗試重新偵測
bool bmePresent();
uint8_t bmeAddress();
bool bmeRead(BmeReading &out);

// 相機 SCCB 與 BME280 共用 GPIO 18/23；相機初始化後必須把腳位矩陣
// 重新綁回 I2C1，否則 BME280 會讀不到。
void bmeRebindBus();
