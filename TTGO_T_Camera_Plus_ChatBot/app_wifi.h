// ============================================================================
//  app_wifi.h - AP + STA Wi-Fi management
// ============================================================================
#ifndef TTGO_T_CAMERA_PLUS_APP_WIFI_H
#define TTGO_T_CAMERA_PLUS_APP_WIFI_H

#include <Arduino.h>

void   wifi_begin();
void   wifi_loop();

bool   wifi_sta_connected();
String wifi_ip();
String wifi_ap_ssid();
String wifi_ap_ip();
String wifi_hostname();

String wifi_status_json();
String wifi_scan_json();

bool   wifi_connect(const String &ssid, const String &password, String &err);
bool   wifi_forget(String &err);

#endif  // TTGO_T_CAMERA_PLUS_APP_WIFI_H
