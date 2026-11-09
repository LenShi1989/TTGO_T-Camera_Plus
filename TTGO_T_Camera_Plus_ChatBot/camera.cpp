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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board_config.h"
#include "display.h"
#include "storage.h"

#ifndef CAMERA_FRAME_SIZE
#define CAMERA_FRAME_SIZE FRAMESIZE_SVGA   // 800x600 JPEG is plenty for snapshots
#endif

#ifndef CAMERA_JPEG_QUALITY
#define CAMERA_JPEG_QUALITY 12             // 0..63, lower = better quality
#endif

static bool s_ready = false;

// Configuration used by camera_begin(); needed to switch to the RGB565
// preview mode and back to JPEG again.
static camera_config_t s_cfg;
static bool            s_cfg_valid = false;
static uint16_t       *s_preview = nullptr;

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

  s_cfg = cfg;
  esp_err_t res = esp_camera_init(&s_cfg);
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

  s_cfg_valid = true;
  s_ready = true;
  return true;
}

// ---------------------------------------------------------------------------
//  live preview -> 240x180 RGB565 on the TFT
// ---------------------------------------------------------------------------
static bool apply_camera_config(const camera_config_t &cfg) {
  esp_camera_deinit();
  return esp_camera_init(&cfg) == ESP_OK;
}

static void scale_rgb565(const uint16_t *src, int sw, int sh,
                         uint16_t *dst, int dw, int dh) {
  for (int y = 0; y < dh; ++y) {
    int sy = (int)((long)y * sh / dh);
    const uint16_t *srow = src + (long)sy * sw;
    uint16_t *drow = dst + (long)y * dw;
    for (int x = 0; x < dw; ++x) {
      int sx = (int)((long)x * sw / dw);
      drow[x] = srow[sx];
    }
  }
}

bool camera_preview(uint8_t seconds) {
  if (!s_ready || !s_cfg_valid) return false;
  if (seconds < 1)  seconds = 1;
  if (seconds > 10) seconds = 10;

  // The panel is 240x240; QVGA keeps the preview light enough to stay fluid.
  const int dst_w = TFT_WIDTH_PX;
  const int dst_h = 180;
  if (!s_preview) {
    s_preview = (uint16_t *)ps_malloc((size_t)dst_w * dst_h * sizeof(uint16_t));
    if (!s_preview) return false;
  }

  camera_config_t pv = s_cfg;
  pv.pixel_format = PIXFORMAT_RGB565;
  pv.frame_size   = FRAMESIZE_QVGA;
  pv.fb_count     = 1;
  pv.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

  if (!apply_camera_config(pv)) {
    // Put the normal JPEG mode back; the photo may still work.
    s_ready = apply_camera_config(s_cfg);
    return false;
  }

  // Drop the first frames so auto-exposure/white balance can settle.
  for (int i = 0; i < 2; ++i) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
  }

  display_preview_begin();

  uint32_t start = millis();
  uint32_t total = (uint32_t)seconds * 1000UL;
  for (;;) {
    uint32_t elapsed = millis() - start;
    if (elapsed >= total) break;
    int remain = (int)((total - elapsed + 999UL) / 1000UL);

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (fb->format == PIXFORMAT_RGB565 && fb->width && fb->height) {
      scale_rgb565((const uint16_t *)fb->buf, (int)fb->width, (int)fb->height,
                   s_preview, dst_w, dst_h);
      display_preview_frame((const uint8_t *)s_preview, dst_w, dst_h, remain);
    }
    esp_camera_fb_return(fb);
  }

  display_preview_end();

  // Back to JPEG/SVGA for the still photo.  Re-initialising the sensor resets
  // auto-exposure, so drop a few frames before the shutter fires.
  s_ready = apply_camera_config(s_cfg);
  if (!s_ready) return false;
  for (int i = 0; i < 3; ++i) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
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