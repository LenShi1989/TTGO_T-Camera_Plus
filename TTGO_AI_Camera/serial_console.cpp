#include "serial_console.h"

#include <SD.h>

#include "app.h"
#include "app_config.h"
#include "bme280.h"
#include "camera_face.h"
#include "display.h"
#include "gemini.h"
#include "nav.h"
#include "power.h"
#include "storage.h"
#include "util.h"
#include "wifi_manager.h"

static String s_line;

static void printHelp() {
  Serial.println();
  Serial.println("可用指令：");
  Serial.println("  help                     顯示本說明");
  Serial.println("  status                   裝置狀態");
  Serial.println("  sensors                  BME280 讀值");
  Serial.println("  menu                     回到主選單");
  Serial.println("  list                     列出目前選單項目");
  Serial.println("  select <n>               執行第 n 個項目（從 0 起算）");
  Serial.println("  chat <文字>              與 Gemini 文字對話");
  Serial.println("  faces                    列出人臉 ID");
  Serial.println("  face add <名稱>          用相機註冊人臉");
  Serial.println("  face del <名稱>          刪除人臉");
  Serial.println("  face export              匯出人臉 CSV 到 SD 卡");
  Serial.println("  snapshot                 拍照存到 /snapshot.jpg");
  Serial.println("  wifi scan                掃描 WiFi");
  Serial.println("  wifi set <SSID> <密碼>   連線並儲存");
  Serial.println("  wifi clear               清除 WiFi 設定");
  Serial.println("  bright <0-255>           設定螢幕亮度");
  Serial.println("  config                   顯示 config.json");
  Serial.println("  save                     儲存 config.json");
  Serial.println("  reboot                   重新開機");
  Serial.println();
}

static void cmdStatus() {
  Serial.println("---- 系統狀態 ----");
  Serial.printf("WiFi      : %s  %s\n", g_cfg.wifi.ssid.c_str(), wifiStatusText().c_str());
  Serial.printf("IP        : %s\n", wifiIP().c_str());
  Serial.printf("AP        : %s (%s)\n", wifiApSsid().c_str(), wifiApActive() ? "開啟" : "關閉");
  Serial.printf("RSSI      : %d dBm\n", wifiRSSI());
  Serial.printf("Heap      : %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.printf("PSRAM     : %u / %u bytes\n", (unsigned)ESP.getFreePsram(),
                (unsigned)ESP.getPsramSize());
  Serial.printf("SD        : %s\n", storageReady() ? "已掛載" : "未掛載");
  Serial.printf("人臉數    : %d\n", faceCount());
  Serial.printf("BME280    : %s\n", bmePresent() ? "已連接" : "未偵測");
  Serial.printf("電池      : %s\n", powerText().c_str());
  Serial.printf("Gemini    : %s（%s）\n", geminiReady() ? "已設定" : "未設定",
                g_cfg.ai.model.c_str());
  Serial.printf("執行時間  : %lu 秒\n", (unsigned long)(millis() / 1000));
  Serial.println();
}

static void cmdSensors() {
  BmeReading r;
  if (!bmeRead(r)) {
    Serial.println("未偵測到 BME280");
    return;
  }
  Serial.printf("溫度 %.2f °C / 濕度 %.2f %% / 氣壓 %.2f hPa\n", r.temperature, r.humidity,
                r.pressure);
}

static void cmdFaces() {
  if (faceCount() == 0) {
    Serial.println("尚未註冊人臉");
    return;
  }
  for (int i = 0; i < faceCount(); i++) {
    Serial.printf("  [%d] %s\n", i, faceNameAt(i).c_str());
  }
}

static void cmdSnapshot() {
  if (!camBegin()) {
    Serial.println("相機初始化失敗");
    return;
  }
  uint8_t *jpg = nullptr;
  size_t len = 0;
  if (!camSnapshotJpeg(&jpg, &len) || !jpg) {
    Serial.println("拍照失敗");
    return;
  }
  File f = SD.open("/snapshot.jpg", FILE_WRITE);
  if (f) {
    f.write(jpg, len);
    f.close();
    Serial.printf("已儲存 /snapshot.jpg（%u bytes）\n", (unsigned)len);
  } else {
    Serial.println("無法寫入 SD 卡");
  }
  free(jpg);
}

static void cmdWifi(const String &args) {
  if (args.startsWith("scan")) {
    String ssids[16];
    int rssi[16];
    bool secure[16];
    int n = wifiScan(ssids, rssi, secure, 16);
    for (int i = 0; i < n; i++) {
      Serial.printf("  %-24s %4d dBm %s\n", ssids[i].c_str(), rssi[i],
                    secure[i] ? "(加密)" : "(開放)");
    }
    if (n == 0) Serial.println("找不到網路");
    return;
  }
  if (args.startsWith("clear")) {
    wifiClearCredentials();
    Serial.println("已清除 WiFi 設定");
    return;
  }
  if (args.startsWith("set ")) {
    String rest = args.substring(4);
    int sp = rest.indexOf(' ');
    String ssid = sp < 0 ? rest : rest.substring(0, sp);
    String pass = sp < 0 ? String() : rest.substring(sp + 1);
    ssid.trim();
    pass.trim();
    String err;
    Serial.printf("連線到 %s …\n", ssid.c_str());
    if (wifiConnect(ssid, pass, 15000, &err)) {
      Serial.printf("連線成功，IP %s\n", wifiIP().c_str());
    } else {
      Serial.printf("失敗：%s\n", err.c_str());
    }
    return;
  }
  Serial.println("用法：wifi scan | wifi set <SSID> <密碼> | wifi clear");
}

static void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.isEmpty()) return;

  String lower = cmd;
  lower.toLowerCase();

  if (lower == "help" || lower == "?") {
    printHelp();
  } else if (lower == "status") {
    cmdStatus();
  } else if (lower == "sensors") {
    cmdSensors();
  } else if (lower == "menu") {
    nav().mode = MODE_MENU;
    nav().depth = 0;
    navPersist();
    appShowMenu();
  } else if (lower == "list") {
    appShowMenu();
    Serial.println("（選單內容已顯示於 TFT）");
  } else if (lower.startsWith("select ")) {
    int n = lower.substring(7).toInt();
    navSetCursor(nav().depth, (uint8_t)n);
    appShowMenu();
    Serial.printf("已選擇第 %d 項\n", n);
  } else if (lower.startsWith("chat ")) {
    String text = cmd.substring(5);
    Serial.println("思考中…");
    AiReply r;
    if (appAiRun(false, text, nullptr, 0, r, nullptr)) {
      appAddHistory(text, r.reply);
      Serial.println(r.reply);
    } else {
      Serial.printf("失敗：%s\n", r.error.c_str());
    }
  } else if (lower == "faces") {
    cmdFaces();
  } else if (lower.startsWith("face add ")) {
    String name = cmd.substring(9);
    name.trim();
    if (!camBegin()) {
      Serial.println("相機初始化失敗");
      return;
    }
    g_appBusy = true;
    String err;
    Serial.println("註冊中，請面向鏡頭…");
    bool ok = faceEnroll(name, nullptr, err);
    g_appBusy = false;
    if (ok) {
      Serial.println("註冊完成");
    } else {
      Serial.println("失敗：" + err);
    }
  } else if (lower.startsWith("face del ")) {
    String name = cmd.substring(9);
    name.trim();
    Serial.println(faceRemove(name) ? "已刪除" : "找不到此人臉");
  } else if (lower == "face export") {
    String msg;
    faceExportCsv(msg);
    Serial.println(msg);
  } else if (lower == "snapshot") {
    cmdSnapshot();
  } else if (lower.startsWith("wifi")) {
    String args = lower.length() > 5 ? lower.substring(5) : String();
    args.trim();
    // 保留原始大小寫的參數
    String rawArgs = cmd.length() > 5 ? cmd.substring(5) : String();
    rawArgs.trim();
    if (args.startsWith("set ")) {
      cmdWifi(rawArgs);
    } else {
      cmdWifi(args);
    }
  } else if (lower.startsWith("bright ")) {
    int v = lower.substring(7).toInt();
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    g_disp.setBrightness((uint8_t)v);
    g_cfg.display.brightness = v;
    g_cfg.save();
    Serial.printf("亮度設為 %d\n", v);
  } else if (lower == "config") {
    Serial.println(g_cfg.toJson(true));
  } else if (lower == "save") {
    Serial.println(g_cfg.save() ? "已儲存" : "儲存失敗");
  } else if (lower == "reboot") {
    Serial.println("重新開機…");
    delay(200);
    ESP.restart();
  } else {
    Serial.printf("未知指令：%s（輸入 help 查看說明）\n", cmd.c_str());
  }
}

void serialBegin() { printHelp(); }

void serialPoll() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      String cmd = s_line;
      s_line = "";
      handleCommand(cmd);
      Serial.print("> ");
    } else if (c == 8 || c == 127) {
      if (s_line.length()) s_line.remove(s_line.length() - 1);
    } else if (s_line.length() < 400) {
      s_line += c;
    }
  }
}
