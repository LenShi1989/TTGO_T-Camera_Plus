// ============================================================================
//  camera.cpp - OV2640 JPEG capture saved to the SD card
//
//  Pin map comes from esp32-camera-screen-master/main/include/board_def.h.
//  The camera uses its own parallel DVP bus; only the SD write has to take
//  the shared SPI lock (bus_lock).
// ============================================================================
#include "camera.h"

#include <SD.h>
#include <esp_camera.h>

#include "board_config.h"
#include "storage.h"

#ifndef CAMERA_FRAME_SIZE
#define CAMERA_FRAME_SIZE FRAMESIZE_SVGA   // 800x600 JPEG is plenty for snapshots
#endif

#ifndef CAMERA_JPEG_QUALITY
#define CAMERA_JPEG_QUALITY 12             // 0..63, lower = better quality
#endif

static bool s_ready = false;

bool camera_ready() { return s_ready; }

// ---------------------------------------------------------------------------
//  init
// ---------------------------------------------------------------------------
bool camera_begin(String &err) {
  err = "";
  if (s_ready) return true;

  camera_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));

  // The ST7789 backlight uses LEDC channel/timer 0 (ESP32 core 3.x auto
  // allocates channel 0 first), so keep XCLK on a different pair.
  cfg.ledc_channel = LEDC_CHANNEL_1;
  cfg.ledc_timer   = LEDC_TIMER_1;

  cfg.pin_pwdn  = BOARD_CAM_PWDN;
  cfg.pin_reset = BOARD_CAM_RESET;
  cfg.pin_xclk  = BOARD_CAM_XCLK;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  cfg.pin_sccb_sda = BOARD_CAM_SIOD;
  cfg.pin_sccb_scl = BOARD_CAM_SIOC;
#else
  cfg.pin_sscb_sda = BOARD_CAM_SIOD;
  cfg.pin_sscb_scl = BOARD_CAM_SIOC;
#endif

  cfg.pin_d0 = BOARD_CAM_D0;
  cfg.pin_d1 = BOARD_CAM_D1;
  cfg.pin_d2 = BOARD_CAM_D2;
  cfg.pin_d3 = BOARD_CAM_D3;
  cfg.pin_d4 = BOARD_CAM_D4;
  cfg.pin_d5 = BOARD_CAM_D5;
  cfg.pin_d6 = BOARD_CAM_D6;
  cfg.pin_d7 = BOARD_CAM_D7;

  cfg.pin_vsync = BOARD_CAM_VSYNC;
  cfg.pin_href  = BOARD_CAM_HREF;
  cfg.pin_pclk  = BOARD_CAM_PCLK;

  cfg.xclk_freq_hz = BOARD_CAM_XCLK_HZ;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size   = CAMERA_FRAME_SIZE;
  cfg.jpeg_quality = CAMERA_JPEG_QUALITY;
  cfg.fb_count     = psramFound() ? 2 : 1;

  esp_err_t res = esp_camera_init(&cfg);
  if (res != ESP_OK) {
    err  = "相機初始化失敗（0x";
    err += String((unsigned)res, HEX);
    err += "）";
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    // Keep the native orientation; the board's camera is mounted upright.
    s->set_framesize(s, CAMERA_FRAME_SIZE);
  }

  s_ready = true;
  return true;
}

// ---------------------------------------------------------------------------
//  voice intent
// ---------------------------------------------------------------------------
static bool has_any(const String &haystack, const char *const *needles, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (haystack.indexOf(needles[i]) >= 0) return true;
  }
  return false;
}

bool camera_is_photo_command(const String &text) {
  String s = text;
  s.trim();
  if (s.length() == 0) return false;

  // Simplified + traditional spellings that Whisper commonly returns.
  static const char *const kChinese[] = {
    "拍照", "照相", "拍張", "拍一張", "拍個照", "拍一下", "幫我拍",
    "自拍", "攝影", "來張照片", "來張相片", "一張照片", "一張相片",
  };
  if (has_any(s, kChinese, sizeof(kChinese) / sizeof(kChinese[0]))) return true;

  String lower = s;
  lower.toLowerCase();
  static const char *const kEnglish[] = {
    "take a photo", "take photo", "take a picture", "take picture",
    "snapshot", "selfie", "photo",
  };
  return has_any(lower, kEnglish, sizeof(kEnglish) / sizeof(kEnglish[0]));
}

// ---------------------------------------------------------------------------
//  capture -> /photos/photo_XXXX.jpg
// ---------------------------------------------------------------------------
static bool next_photo_path(String &path) {
  for (uint16_t i = 1; i < 10000; ++i) {
    char buf[48];
    snprintf(buf, sizeof(buf), PHOTO_DIR "/" PHOTO_NAME_PREFIX "%04u.jpg",
             (unsigned)i);
    if (!SD.exists(buf)) {
      path = buf;
      return true;
    }
  }
  return false;
}

bool camera_capture_to_sd(String &path, size_t &bytes, String &err) {
  path  = "";
  bytes = 0;
  err   = "";

  if (!s_ready) {
    err = "相機尚未初始化";
    return false;
  }
  if (!sd_ready()) {
    err = "SD 卡未掛載，無法儲存照片";
    return false;
  }

  String derr;
  if (!sd_ensure_dir(PHOTO_DIR, derr)) {
    err = derr.length() ? derr : String("無法建立照片資料夾");
    return false;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    err = "相機擷取失敗";
    return false;
  }
  if (!fb->buf || fb->len == 0) {
    esp_camera_fb_return(fb);
    err = "相機沒有回傳影像";
    return false;
  }

  const size_t len = fb->len;

  if (!bus_lock(8000)) {
    esp_camera_fb_return(fb);
    err = "SD 卡忙碌中";
    return false;
  }

  String target;
  if (!next_photo_path(target)) {
    bus_unlock();
    esp_camera_fb_return(fb);
    err = "照片編號已用完，請清理 /photos";
    return false;
  }

  File f = SD.open(target, "w");
  if (!f) {
    bus_unlock();
    esp_camera_fb_return(fb);
    err = "無法建立照片檔 " + target;
    return false;
  }

  size_t written = f.write(fb->buf, len);
  f.close();
  bus_unlock();
  esp_camera_fb_return(fb);

  if (written != len) {
    err = "照片寫入不完整";
    return false;
  }

  path  = target;
  bytes = len;
  return true;
}