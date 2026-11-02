#include "bme280.h"

#include <Wire.h>

#include "app_config.h"
#include "board_pins.h"
#include "util.h"

#define BME_I2C_HZ 100000
#define BME_RETRY_MS 3000

static bool s_present = false;
static bool s_busReady = false;
static bool s_hasHumidity = true; // BME280 = true、BMP280 = false
static uint8_t s_addr = 0x76;
static uint32_t s_lastProbe = 0;

static uint16_t dig_T1;
static int16_t dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static uint8_t dig_H1, dig_H3;
static int16_t dig_H2, dig_H4, dig_H5;
static int8_t dig_H6;
static double t_fine = 0;

static bool busBegin() {
  if (s_busReady) return true;
  if (!Wire1.begin(PIN_I2C_SDA, PIN_I2C_SCL, BME_I2C_HZ)) {
    logLine("BME", "I2C1 初始化失敗（SDA %d / SCL %d）", PIN_I2C_SDA, PIN_I2C_SCL);
    return false;
  }
  s_busReady = true;
  delay(20);
  return true;
}

void bmeRebindBus() {
  // 相機 SCCB 使用 I2C0，BME280 使用 I2C1，但兩者都在 GPIO 18/23。
  // GPIO 矩陣同一個輸出腳位只能綁一個訊號，所以相機初始化後要把
  // I2C1 重新綁回來，BME280 才不會突然讀不到。
  // 匯流排偶爾正忙時 begin() 會失敗，重試幾次即可。
  for (int attempt = 0; attempt < 3; attempt++) {
    if (Wire1.begin(PIN_I2C_SDA, PIN_I2C_SCL, BME_I2C_HZ)) {
      s_busReady = true;
      delay(5);
      return;
    }
    delay(10);
  }
  s_busReady = false;
  logLine("BME", "重新綁定 I2C1 失敗（GPIO %d/%d）", PIN_I2C_SDA, PIN_I2C_SCL);
}

// 找不到感測器時，掃描常見位址，方便使用者確認接線 / 位址。
static void scanCandidates() {
  static const uint8_t addrs[] = {0x76, 0x77, 0x68, 0x69, 0x55, 0x5D, 0x5C};
  String hits;
  for (size_t i = 0; i < sizeof(addrs); i++) {
    if (!s_busReady && !busBegin()) break;
    Wire1.beginTransmission(addrs[i]);
    if (Wire1.endTransmission() == 0) {
      if (hits.length()) hits += ", ";
      hits += "0x" + String(addrs[i], HEX);
    }
  }
  if (hits.length()) {
    logLine("BME", "I2C 掃描有回應：%s（BME280 應為 0x76/0x77）", hits.c_str());
  } else {
    logLine("BME", "I2C 掃描無任何回應，請檢查 SDA %d / SCL %d 與感測器供電",
            PIN_I2C_SDA, PIN_I2C_SCL);
  }
}

static bool readRegs(uint8_t reg, uint8_t *buf, size_t len) {
  Wire1.beginTransmission(s_addr);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  size_t got = Wire1.requestFrom((int)s_addr, (int)len);
  if (got != len) return false;
  for (size_t i = 0; i < len; i++) {
    if (!Wire1.available()) return false;
    buf[i] = (uint8_t)Wire1.read();
  }
  return true;
}

static bool readReg(uint8_t reg, uint8_t &value) { return readRegs(reg, &value, 1); }

static bool writeReg(uint8_t reg, uint8_t value) {
  Wire1.beginTransmission(s_addr);
  Wire1.write(reg);
  Wire1.write(value);
  return Wire1.endTransmission() == 0;
}

static bool probe(uint8_t addr) {
  Wire1.beginTransmission(addr);
  if (Wire1.endTransmission() != 0) return false;
  s_addr = addr;
  uint8_t id = 0;
  if (!readReg(0xD0, id)) return false;
  // 0x60 = BME280；0x58/0x56/0x57 = BMP280（沒有濕度）
  if (id != 0x60 && id != 0x58 && id != 0x56 && id != 0x57) {
    logLine("BME", "0x%02X 有回應，但晶片 ID 0x%02X 不支援", addr, id);
    return false;
  }
  s_hasHumidity = (id == 0x60);
  logLine("BME", "偵測到 0x%02X（chip id 0x%02X，%s）", addr, id,
          s_hasHumidity ? "BME280" : "BMP280");
  return true;
}

bool bmeBegin() {
  if (s_present) return true;
  if (!g_cfg.bme.enabled) {
    logLine("BME", "設定中已停用");
    return false;
  }
  if (!busBegin()) {
    s_lastProbe = millis();
    return false;
  }

  bool found = false;
  uint8_t first = g_cfg.bme.address;
  uint8_t second = (first == 0x76) ? 0x77 : 0x76;
  if (probe(first)) {
    found = true;
  } else if (probe(second)) {
    found = true;
  }
  if (!found) {
    logLine("BME", "未偵測到 BME280/BMP280（I2C 0x%02X/0x%02X 均無回應）", first, second);
    scanCandidates();
    s_present = false;
    s_lastProbe = millis();
    return false;
  }

  if (!writeReg(0xE0, 0xB6)) { // 軟體重置
    logLine("BME", "軟體重置失敗");
    s_lastProbe = millis();
    return false;
  }
  delay(10);

  uint8_t c[26];
  if (!readRegs(0x88, c, 26)) {
    logLine("BME", "校正資料讀取失敗");
    s_lastProbe = millis();
    return false;
  }
  dig_T1 = (uint16_t)(c[0] | (c[1] << 8));
  dig_T2 = (int16_t)(c[2] | (c[3] << 8));
  dig_T3 = (int16_t)(c[4] | (c[5] << 8));
  dig_P1 = (uint16_t)(c[6] | (c[7] << 8));
  dig_P2 = (int16_t)(c[8] | (c[9] << 8));
  dig_P3 = (int16_t)(c[10] | (c[11] << 8));
  dig_P4 = (int16_t)(c[12] | (c[13] << 8));
  dig_P5 = (int16_t)(c[14] | (c[15] << 8));
  dig_P6 = (int16_t)(c[16] | (c[17] << 8));
  dig_P7 = (int16_t)(c[18] | (c[19] << 8));
  dig_P8 = (int16_t)(c[20] | (c[21] << 8));
  dig_P9 = (int16_t)(c[22] | (c[23] << 8));
  dig_H1 = c[25];

  if (s_hasHumidity) {
    uint8_t h[7];
    if (!readRegs(0xE1, h, 7)) {
      logLine("BME", "濕度校正資料讀取失敗");
      s_lastProbe = millis();
      return false;
    }
    dig_H2 = (int16_t)(h[0] | (h[1] << 8));
    dig_H3 = h[2];
    dig_H4 = (int16_t)((h[3] << 4) | (h[4] & 0x0F));
    dig_H5 = (int16_t)((h[5] << 4) | (h[4] >> 4));
    dig_H6 = (int8_t)h[6];
    writeReg(0xF2, 0x01); // 濕度取樣 x1
  }

  writeReg(0xF5, 0xA8); // 1 秒待機、濾波 x4
  writeReg(0xF4, 0x57); // 溫度 x2、氣壓 x16、normal 模式

  s_present = true;
  logLine("BME", "%s 就緒（0x%02X）", s_hasHumidity ? "BME280" : "BMP280", s_addr);
  return true;
}

bool bmeEnsure() {
  if (s_present) return true;
  if (!g_cfg.bme.enabled) return false;
  if ((uint32_t)(millis() - s_lastProbe) < BME_RETRY_MS) return false;
  return bmeBegin();
}

bool bmePresent() { return s_present; }

uint8_t bmeAddress() { return s_addr; }

bool bmeRead(BmeReading &out) {
  out = BmeReading();
  if (!s_present && !bmeEnsure()) return false;

  uint8_t d[8];
  size_t need = s_hasHumidity ? 8 : 6;
  if (!readRegs(0xF7, d, need)) {
    // 讀取失敗最常見的原因是相機初始化時把 GPIO 18/23 搶回 I2C0。
    // 重新綁定 I2C1 後立刻再試一次，成功就繼續使用，不必重新開機。
    bmeRebindBus();
    if (!readRegs(0xF7, d, need)) {
      logLine("BME", "讀取失敗，3 秒後重新偵測");
      s_present = false;
      s_lastProbe = millis();
      return false;
    }
  }

  int32_t adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
  int32_t adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);

  double var1 = ((double)adc_T / 16384.0 - (double)dig_T1 / 1024.0) * (double)dig_T2;
  double var2 = ((double)adc_T / 131072.0 - (double)dig_T1 / 8192.0);
  var2 = var2 * var2 * (double)dig_T3;
  t_fine = var1 + var2;
  double temperature = t_fine / 5120.0;

  double p = 0;
  if (dig_P1 != 0) {
    double v1 = t_fine / 2.0 - 64000.0;
    double v2 = v1 * v1 * (double)dig_P6 / 32768.0;
    v2 = v2 + v1 * (double)dig_P5 * 2.0;
    v2 = v2 / 4.0 + (double)dig_P4 * 65536.0;
    v1 = ((double)dig_P3 * v1 * v1 / 524288.0 + (double)dig_P2 * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * (double)dig_P1;
    if (v1 != 0) {
      p = 1048576.0 - (double)adc_P;
      p = (p - v2 / 4096.0) * 6250.0 / v1;
      v1 = (double)dig_P9 * p * p / 2147483648.0;
      v2 = p * (double)dig_P8 / 32768.0;
      p = p + (v1 + v2 + (double)dig_P7) / 16.0;
    }
  }

  double h = 0;
  if (s_hasHumidity) {
    int32_t adc_H = ((int32_t)d[6] << 8) | d[7];
    double vh = t_fine - 76800.0;
    vh = ((double)adc_H - ((double)dig_H4 * 64.0 + (double)dig_H5 / 16384.0 * vh)) *
         ((double)dig_H2 / 65536.0 *
          (1.0 + (double)dig_H6 / 67108864.0 * vh *
                     (1.0 + (double)dig_H3 / 67108864.0 * vh)));
    vh = vh * (1.0 - (double)dig_H1 * vh / 524288.0);
    if (vh > 100.0) vh = 100.0;
    if (vh < 0.0) vh = 0.0;
    h = vh;
  }

  out.valid = true;
  out.temperature = (float)temperature;
  out.pressure = (float)(p / 100.0);
  out.humidity = (float)h;
  out.stamp = millis();
  return true;
}
