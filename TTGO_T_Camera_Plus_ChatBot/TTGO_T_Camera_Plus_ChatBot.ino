// ============================================================================
//  TTGO_T_Camera_Plus_ChatBot.ino
//
//  AI chat assistant for the TTGO T-Camera Plus (ESP32-DOWDQ6, 4 MB flash,
//  8 MB PSRAM, ST7789 240x240, microSD, I2S microphone).
//
//  Version ................. 1.7.0  (APP_VERSION in board_config.h)
//  Hardware map ............ board_config.h
//  Persistent settings ..... /sd/config.json  (SPIFFS fallback)
//  Web UI .................. data/  -> SPIFFS
//  TFT driver setup ........ tft_setup.h (picked up by TFT_eSPI)
//
//  Arduino IDE 1.8.19:
//    Board            : ESP32 Wrover Module (or "ESP32 Dev Module")
//    Flash Size       : 4MB
//    Partition Scheme : Huge APP (3MB No OTA/1MB SPIFFS)
//    PSRAM            : Enabled
//    Upload Speed     : 115200
//
//  Libraries:
//    TFT_eSPI 2.5.43
//    ArduinoJson >= 6.21 (tested with 7.x)
//  Board package:
//    esp32 by Espressif 2.0.17 (recommended) - 3.x also compiles
// ============================================================================
#include <Arduino.h>

#include "app_config.h"
#include "app_wifi.h"
#include "board_config.h"
#include "camera.h"
#include "chat.h"
#include "display.h"
#include "font_tc.h"
#include "jobs.h"
#include "storage.h"
#include "web_ui.h"

// ---------------------------------------------------------------------------
//  setup
// ---------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("==============================================");
  Serial.printf("  %s  v%s\n", APP_NAME, APP_VERSION);
  Serial.println("==============================================");

  // GPIO0 is the SD card chip select *and* a boot strapping pin.  Drive it
  // high before anything else so the card is deselected during boot.
  pinMode(BOARD_SD_CS, OUTPUT);
  digitalWrite(BOARD_SD_CS, HIGH);
  delay(20);

  randomSeed((uint32_t)micros());

  // 1. panel first: tft.init() configures the shared VSPI bus (MISO/MOSI/SCLK)
  //    and attaches the backlight PWM (LEDC channel 0 on core 3.x).
  display_begin();

  // 2. filesystems: SPIFFS for the web assets, SD on the bus opened above
  storage_begin();

  // 3. settings (/sd/config.json)
  config_begin();

  // 4. apply rotation / brightness / face preference, then load the font
  display_apply_config();
  font_load();

  // 5. splash
  display_boot_screen();

  // 6. OV2640 for the voice photo command.  Init after the panel so the
  //    camera can pick LEDC channel/timer 1 without disturbing the backlight.
  //    A missing camera is not fatal - everything else keeps working.
  {
    String cam_err;
    if (camera_begin(cam_err)) {
      Serial.println("[camera] OV2640 ready (JPEG)");
    } else {
      Serial.printf("[camera] unavailable: %s\n", cam_err.c_str());
    }
  }

  // 7. network (AP always on, STA when credentials exist)
  wifi_begin();

  // 8. AI worker + chat history
  chat_begin();
  jobs_begin();

  // 9. HTTP server
  web_begin();

  display_set_face(FACE_IDLE);
  display_toast(String("網頁：") + wifi_ap_ip(), display_tft().color565(45, 212, 255));

  Serial.println("[boot] ready");
  Serial.printf("[boot] free heap %u, PSRAM %u / %u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram(),
                (unsigned)ESP.getPsramSize());
}

// ---------------------------------------------------------------------------
//  loop
// ---------------------------------------------------------------------------
void loop()
{
  web_loop();         // keep the HTTP server responsive
  wifi_loop();        // reconnect watchdog + NTP on first connect
  display_poll_job(); // mirror the AI worker state on the TFT
  display_tick();     // face animation / clock / text refresh
  delay(2);
}
