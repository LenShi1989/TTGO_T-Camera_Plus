#pragma once
#include <Arduino.h>

bool wifiBegin();
bool wifiConnected();
String wifiIP();
int wifiRSSI();
String wifiStatusText();
String wifiApSsid();
bool wifiApActive();

// 掃描結果（最多 maxItems 筆），回傳實際數量
int wifiScan(String *ssids, int *rssi, bool *secure, int maxItems);

bool wifiConnect(const String &ssid, const String &password, uint32_t timeoutMs, String *err);
void wifiClearCredentials();
void wifiRestartAp();
void wifiLoop();
