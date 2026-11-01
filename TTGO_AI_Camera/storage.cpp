#include "storage.h"

#include "board_pins.h"
#include "util.h"

SPIClass spiBus(HSPI);

static bool s_ready = false;

bool storageBegin() {
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  // 先以自訂腳位啟動 HSPI；SD.begin() 內部再次呼叫 begin() 時會直接返回
  spiBus.begin(PIN_SPI_SCLK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);

  for (int attempt = 0; attempt < 3 && !s_ready; attempt++) {
    if (SD.begin(PIN_SD_CS, spiBus, 4000000)) {
      s_ready = true;
      break;
    }
    logLine("SD", "掛載失敗，重試 %d", attempt + 1);
    SD.end();
    delay(150);
  }

  if (!s_ready) {
    logLine("SD", "找不到 SD 卡");
    return false;
  }
  logLine("SD", "SD 卡已掛載");
  return true;
}

bool storageReady() { return s_ready; }

void storageInfo(uint64_t &totalBytes, uint64_t &usedBytes) {
  totalBytes = 0;
  usedBytes = 0;
  if (!s_ready) return;
  totalBytes = SD.totalBytes();
  usedBytes = SD.usedBytes();
}

String storageRead(const String &path) {
  if (!s_ready) return String();
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) return String();
  size_t n = f.size();
  String out;
  out.reserve(n + 1);
  uint8_t buf[256];
  while (f.available()) {
    size_t r = f.read(buf, sizeof(buf));
    if (r == 0) break;
    for (size_t i = 0; i < r; i++) out += (char)buf[i];
  }
  f.close();
  return out;
}

bool storageWrite(const String &path, const String &data) {
  if (!s_ready) return false;
  File f = SD.open(path.c_str(), FILE_WRITE);
  if (!f) {
    logLine("SD", "無法寫入 %s", path.c_str());
    return false;
  }
  size_t written = f.write((const uint8_t *)data.c_str(), data.length());
  f.close();
  return written == data.length();
}

bool storageAppend(const String &path, const String &data) {
  if (!s_ready) return false;
  File f = SD.open(path.c_str(), FILE_APPEND);
  if (!f) return false;
  size_t written = f.write((const uint8_t *)data.c_str(), data.length());
  f.close();
  return written == data.length();
}

bool storageExists(const String &path) {
  if (!s_ready) return false;
  return SD.exists(path.c_str());
}

bool storageRemove(const String &path) {
  if (!s_ready) return false;
  return SD.remove(path.c_str());
}
