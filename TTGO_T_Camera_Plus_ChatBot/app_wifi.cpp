// ============================================================================
//  app_wifi.cpp - the device always runs AP+STA:
//    * the soft AP stays reachable at 192.168.4.1 for configuration
//    * the station connects to the router using the SD card config
// ============================================================================
#include "app_wifi.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <algorithm>
#include <time.h>
#include <vector>

#include "app_config.h"
#include "board_config.h"
#include "storage.h"

static String  s_ap_ssid;
static bool    s_was_connected = false;
static bool    s_mdns_ok       = false;
static uint32_t s_last_retry   = 0;

static String mac_suffix() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[8];
  snprintf(buf, sizeof(buf), "%02X%02X", mac[4], mac[5]);
  return String(buf);
}

String wifi_hostname() { return config_device_slug(); }
String wifi_ap_ssid()  { return s_ap_ssid; }
String wifi_ap_ip()    { return WiFi.softAPIP().toString(); }
String wifi_ip()       { return WiFi.localIP().toString(); }
bool   wifi_sta_connected() { return WiFi.status() == WL_CONNECTED; }

static void start_time() {
  configTzTime("CST-8", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

void wifi_begin() {
  AppConfig cfg;
  config_snapshot(cfg);

  String host = config_device_slug();
  s_ap_ssid = host + "-" + mac_suffix();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(host.c_str());
  WiFi.setSleep(false);

  String pw = cfg.ap_password;
  if (pw.length() < 8) pw = "12345678";

  bool ap_ok = WiFi.softAP(s_ap_ssid.c_str(), pw.c_str());
  Serial.printf("[wifi] AP %s (%s) ap_ip=%s\n", s_ap_ssid.c_str(), ap_ok ? "ok" : "FAILED",
                WiFi.softAPIP().toString().c_str());

  if (cfg.wifi_ssid.length()) {
    Serial.printf("[wifi] connecting to \"%s\" ...\n", cfg.wifi_ssid.c_str());
    WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_password.c_str());
  } else {
    Serial.println("[wifi] no station credentials, AP only");
  }

  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    s_mdns_ok = true;
    Serial.printf("[wifi] mDNS http://%s.local/\n", host.c_str());
  }
}

void wifi_loop() {
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected && !s_was_connected) {
    s_was_connected = true;
    start_time();
    Serial.printf("[wifi] station connected: %s  RSSI %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else if (!connected) {
    s_was_connected = false;
    AppConfig cfg;
    config_snapshot(cfg);
    if (cfg.wifi_ssid.length() && millis() - s_last_retry > 30000) {
      s_last_retry = millis();
      Serial.printf("[wifi] retrying \"%s\"\n", cfg.wifi_ssid.c_str());
      WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_password.c_str());
    }
  }
}

String wifi_status_json() {
  AppConfig cfg;
  config_snapshot(cfg);

  bool connected = wifi_sta_connected();
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macbuf[20];
  snprintf(macbuf, sizeof(macbuf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  String out;
  out.reserve(400);
  out  = "{\"sta_connected\":";
  out += connected ? "true" : "false";
  out += ",\"ssid\":\"";
  out += json_escape(connected ? WiFi.SSID() : cfg.wifi_ssid);
  out += "\",\"ip\":\"";
  out += connected ? WiFi.localIP().toString() : String("");
  out += "\",\"rssi\":";
  out += connected ? WiFi.RSSI() : 0;
  out += ",\"hostname\":\"";
  out += json_escape(wifi_hostname());
  out += "\",\"ap_ssid\":\"";
  out += json_escape(s_ap_ssid);
  out += "\",\"ap_ip\":\"";
  out += WiFi.softAPIP().toString();
  out += "\",\"ap_clients\":";
  out += WiFi.softAPgetStationNum();
  out += ",\"mac\":\"";
  out += macbuf;
  out += "\"}";
  return out;
}

String wifi_scan_json() {
  int found = WiFi.scanNetworks(false, true);
  if (found < 0) {
    return "{\"ok\":false,\"error\":\"掃描失敗，請稍後再試\"}";
  }

  struct Net {
    String ssid;
    int rssi;
    int channel;
    bool secure;
  };
  std::vector<Net> nets;
  nets.reserve(found);

  for (int i = 0; i < found; ++i) {
    String ssid = WiFi.SSID(i);
    ssid.trim();
    if (ssid.length() == 0) continue;
    bool duplicate = false;
    for (auto &n : nets) {
      if (n.ssid == ssid) {
        duplicate = true;
        if (WiFi.RSSI(i) > n.rssi) {
          n.rssi = WiFi.RSSI(i);
          n.channel = WiFi.channel(i);
          n.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        break;
      }
    }
    if (!duplicate) {
      Net n;
      n.ssid = ssid;
      n.rssi = WiFi.RSSI(i);
      n.channel = WiFi.channel(i);
      n.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      nets.push_back(n);
    }
  }
  WiFi.scanDelete();

  std::sort(nets.begin(), nets.end(), [](const Net &a, const Net &b) {
    return a.rssi > b.rssi;
  });
  if (nets.size() > 40) nets.resize(40);

  String out = "{\"ok\":true,\"networks\":[";
  for (size_t i = 0; i < nets.size(); ++i) {
    if (i) out += ',';
    out += "{\"ssid\":\"";
    out += json_escape(nets[i].ssid);
    out += "\",\"rssi\":";
    out += nets[i].rssi;
    out += ",\"channel\":";
    out += nets[i].channel;
    out += ",\"secure\":";
    out += nets[i].secure ? "true" : "false";
    out += "}";
  }
  out += "]}";
  return out;
}

bool wifi_connect(const String &ssid, const String &password, String &err) {
  String name = ssid;
  name.trim();
  if (name.length() == 0) {
    err = "請輸入 SSID";
    return false;
  }
  if (name.length() > 32) {
    err = "SSID 長度不可超過 32 個字元";
    return false;
  }
  if (password.length() > 64) {
    err = "密碼長度不可超過 64 個字元";
    return false;
  }

  String cfg_err;
  if (!config_set_wifi(name, password, cfg_err)) {
    Serial.printf("[wifi] could not persist credentials: %s\n", cfg_err.c_str());
  }

  WiFi.disconnect(false, false);
  delay(250);
  WiFi.begin(name.c_str(), password.c_str());

  uint32_t start = millis();
  while (millis() - start < 12000) {
    if (WiFi.status() == WL_CONNECTED) {
      s_was_connected = true;
      start_time();
      err = "";
      return true;
    }
    delay(150);
  }

  err = "連線逾時：請確認 SSID、密碼與訊號強度";
  return false;
}

bool wifi_forget(String &err) {
  if (!config_set_wifi("", "", err)) return false;
  WiFi.disconnect(false, false);
  s_was_connected = false;
  s_last_retry = millis();
  return true;
}
