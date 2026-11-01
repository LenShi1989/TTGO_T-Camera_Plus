#include "wifi_manager.h"

#include <WiFi.h>

#include "app_config.h"
#include "util.h"

static bool s_apActive = false;
static String s_apSsid;

static String buildApSsid() {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "-%02X%02X", mac[4], mac[5]);
  return g_cfg.wifi.ap_ssid + suffix;
}

void wifiRestartAp() {
  WiFi.mode(WIFI_AP_STA);
  s_apSsid = buildApSsid();
  const char *pass = g_cfg.wifi.ap_password.length() >= 8 ? g_cfg.wifi.ap_password.c_str() : nullptr;
  s_apActive = WiFi.softAP(s_apSsid.c_str(), pass);
  logLine("WIFI", "AP %s 已啟動：http://%s", s_apSsid.c_str(),
          WiFi.softAPIP().toString().c_str());
}

bool wifiBegin() {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);

  if (g_cfg.wifi.ssid.isEmpty()) {
    logLine("WIFI", "未設定 SSID，啟動 AP 模式");
    wifiRestartAp();
    return false;
  }

  WiFi.begin(g_cfg.wifi.ssid.c_str(), g_cfg.wifi.password.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < g_cfg.wifi.connect_timeout_ms) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    logLine("WIFI", "已連線 %s，IP %s", g_cfg.wifi.ssid.c_str(),
            WiFi.localIP().toString().c_str());
    return true;
  }

  logLine("WIFI", "連線 %s 失敗，啟動 AP 模式", g_cfg.wifi.ssid.c_str());
  wifiRestartAp();
  return false;
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

String wifiIP() { return WiFi.localIP().toString(); }

int wifiRSSI() { return WiFi.RSSI(); }

String wifiStatusText() {
  switch (WiFi.status()) {
    case WL_CONNECTED:
      return "已連線";
    case WL_NO_SSID_AVAIL:
      return "找不到基地台";
    case WL_CONNECT_FAILED:
      return "連線失敗";
    case WL_CONNECTION_LOST:
      return "連線中斷";
    case WL_DISCONNECTED:
      return "未連線";
    default:
      return "狀態 " + String((int)WiFi.status());
  }
}

String wifiApSsid() { return s_apSsid; }

bool wifiApActive() { return s_apActive; }

int wifiScan(String *ssids, int *rssi, bool *secure, int maxItems) {
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) return 0;
  int out = 0;
  for (int i = 0; i < n && out < maxItems; i++) {
    String s = WiFi.SSID(i);
    if (s.isEmpty()) continue;
    ssids[out] = s;
    rssi[out] = WiFi.RSSI(i);
    secure[out] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    out++;
  }
  WiFi.scanDelete();
  return out;
}

bool wifiConnect(const String &ssid, const String &password, uint32_t timeoutMs, String *err) {
  logLine("WIFI", "嘗試連線 %s", ssid.c_str());
  WiFi.disconnect(true, false);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    g_cfg.wifi.ssid = ssid;
    g_cfg.wifi.password = password;
    g_cfg.save();
    logLine("WIFI", "連線成功，IP %s", WiFi.localIP().toString().c_str());
    return true;
  }
  if (err) *err = "連線逾時，請確認 SSID 與密碼";
  logLine("WIFI", "連線 %s 失敗", ssid.c_str());
  wifiRestartAp();
  return false;
}

void wifiClearCredentials() {
  g_cfg.wifi.ssid = "";
  g_cfg.wifi.password = "";
  g_cfg.save();
  WiFi.disconnect(true, true);
  logLine("WIFI", "已清除連線設定");
}

void wifiLoop() {
  static uint32_t last = 0;
  if (millis() - last < 5000) return;
  last = millis();
  if (!s_apActive && !g_cfg.wifi.ssid.isEmpty() && !wifiConnected()) {
    // 若曾經連上又掉線，重新嘗試
    static uint32_t fails = 0;
    if (fails++ < 3) {
      logLine("WIFI", "重新連線中…");
      WiFi.reconnect();
    }
  }
}
