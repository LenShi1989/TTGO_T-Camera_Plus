#include "power.h"

#include <Wire.h>

#include "board_pins.h"
#include "util.h"

// GPIO 18/23 同時是相機 SCCB（I2C0）與 IP5306（I2C1）的接腳。
// 相機初始化會把 GPIO 矩陣搶回 I2C0，所以這裡要能自行重新綁定 I2C1。
static bool s_busReady = false;

static bool busBegin() {
  s_busReady = Wire1.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  return s_busReady;
}

void powerBegin() { busBegin(); }

static bool readReg(uint8_t reg, uint8_t &value) {
  Wire1.beginTransmission(IP5306_ADDR);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((int)IP5306_ADDR, 1) != 1) return false;
  if (!Wire1.available()) return false;
  value = (uint8_t)Wire1.read();
  return true;
}

bool powerRead(PowerInfo &out) {
  out = PowerInfo();

  if (!s_busReady) busBegin();

  uint8_t r70 = 0, r78 = 0;
  bool ok70 = readReg(0x70, r70);
  bool ok78 = readReg(0x78, r78);
  if (!ok70 && !ok78) {
    // 多半是相機初始化把 18/23 拿走：重新綁定 I2C1 再試一次
    if (busBegin()) {
      ok70 = readReg(0x70, r70);
      ok78 = readReg(0x78, r78);
    }
  }
  if (!ok70 && !ok78) return false;

  out.ok = true;
  out.charging = ok70 && (r70 & 0x08);
  if (ok78) {
    uint8_t lvl = (r78 >> 4) & 0x07;
    static const int pct[5] = {0, 25, 50, 75, 100};
    out.percent = lvl <= 4 ? pct[lvl] : -1;
    out.full = (r78 & 0x80) != 0;
  }
  return true;
}

String powerText() {
  PowerInfo p;
  if (!powerRead(p)) return "未知";
  String s;
  if (p.percent >= 0) {
    s = String(p.percent) + "%";
  } else {
    s = "未知";
  }
  if (p.charging) s += "（充電中）";
  return s;
}
