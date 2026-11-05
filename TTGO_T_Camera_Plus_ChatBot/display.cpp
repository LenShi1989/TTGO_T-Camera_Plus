// ============================================================================
//  display.cpp - TFT UI: status bar, animated AI face, chat text area
// ============================================================================
#include "display.h"

#include <WiFi.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "app_config.h"
#include "audio.h"
#include "board_config.h"
#include "font_tc.h"
#include "jobs.h"
#include "storage.h"

static TFT_eSPI tft;

// palette -------------------------------------------------------------------
static uint16_t C_BG, C_PANEL, C_PANEL2, C_BORDER, C_TEXT, C_DIM;
static uint16_t C_ACCENT, C_ACCENT2, C_GOOD, C_WARN, C_BAD, C_EYE, C_IRIS;

static void palette() {
  C_BG      = tft.color565(8, 11, 18);
  C_PANEL   = tft.color565(17, 27, 46);
  C_PANEL2  = tft.color565(24, 38, 64);
  C_BORDER  = tft.color565(45, 90, 150);
  C_TEXT    = tft.color565(226, 236, 250);
  C_DIM     = tft.color565(118, 138, 168);
  C_ACCENT  = tft.color565(45, 212, 255);
  C_ACCENT2 = tft.color565(110, 120, 255);
  C_GOOD    = tft.color565(60, 220, 150);
  C_WARN    = tft.color565(255, 186, 70);
  C_BAD     = tft.color565(255, 96, 110);
  C_EYE     = tft.color565(236, 246, 255);
  C_IRIS    = tft.color565(70, 205, 245);
}

// state ---------------------------------------------------------------------
static FaceExpression g_face      = FACE_IDLE;
static uint32_t       g_face_until = 0;
static bool           g_face_persistent = true;
static bool           g_face_dirty = true;
static String         g_speech;
static bool           g_speech_dirty = true;
static uint32_t       g_speak_until = 0;
static uint32_t       g_last_frame = 0;
static uint32_t       g_next_blink = 0;
static uint32_t       g_blink_at = 0;
static bool           g_blinking = false;
static uint32_t       g_last_status = 0;
static bool           g_face_enabled = true;

static String         g_toast;
static uint16_t       g_toast_color = 0;
static uint32_t       g_toast_until = 0;
static bool           g_toast_active = false;
static bool           g_toast_dirty = true;

static uint32_t       g_boot_ms = 0;

// ---------------------------------------------------------------------------
//  small helpers
// ---------------------------------------------------------------------------
static void arc_dots(int cx, int cy, int w, int up, int radius, int dot_r, uint16_t color) {
  const int steps = 22;
  for (int i = 0; i <= steps; ++i) {
    float a = (float)M_PI * (float)i / (float)steps;
    int x = cx - w / 2 + (int)(w * i / (float)steps);
    int y = cy + (up ? -(int)(sinf(a) * radius) : (int)(sinf(a) * radius));
    tft.fillCircle(x, y, dot_r, color);
  }
}

static void draw_level_bars(int cx, int cy, int level) {
  const int bars = 9;
  const int step = 8;
  const int max_h = 18;
  int start = cx - (bars * step) / 2;
  for (int i = 0; i < bars; ++i) {
    int base = (int)((float)max_h * (0.25f + 0.75f * fabsf(sinf((float)(i + 1) * 0.6f))));
    int hh = base * level / 100;
    if (hh < 2) hh = 2;
    uint16_t c = (level > 70) ? C_GOOD : (level > 30 ? C_ACCENT : C_PANEL2);
    tft.fillRoundRect(start + i * step, cy - hh / 2, 5, hh, 2, c);
  }
}

// ---------------------------------------------------------------------------
//  face
// ---------------------------------------------------------------------------
static void draw_eye_normal(int cx, int cy, int w, int h, bool blink, int look_x, int look_y) {
  if (blink) {
    tft.fillRoundRect(cx - w / 2, cy - 2, w, 5, 2, C_IRIS);
    return;
  }
  tft.fillRoundRect(cx - w / 2, cy - h / 2, w, h, w / 2, C_EYE);
  int px = cx + look_x;
  int py = cy + look_y;
  tft.fillCircle(px, py, h / 5 + 2, C_IRIS);
  tft.fillCircle(px - 3, py - 3, 2, C_EYE);
}

static void draw_eye_happy(int cx, int cy) {
  arc_dots(cx, cy + 6, 30, 1, 10, 2, C_IRIS);
}

static void draw_eye_cross(int cx, int cy) {
  for (int k = -1; k <= 1; ++k) {
    tft.drawLine(cx - 12, cy - 12 + k, cx + 12, cy + 12 + k, C_BAD);
    tft.drawLine(cx - 12, cy + 12 + k, cx + 12, cy - 12 + k, C_BAD);
  }
}

static void draw_mouth(FaceExpression face, uint32_t now) {
  const int cx = 120;
  const int cy = 102;
  switch (face) {
    case FACE_LISTEN:
      tft.fillCircle(cx, cy, 6, C_IRIS);
      draw_level_bars(cx, 116, audio_level());
      break;
    case FACE_THINK: {
      int phase = (int)((now / 220) % 4);
      for (int i = 0; i < 3; ++i) {
        uint16_t c = (i <= phase - 1) ? C_ACCENT : C_PANEL2;
        tft.fillCircle(cx - 16 + i * 16, cy, 4, c);
      }
      break;
    }
    case FACE_SPEAK: {
      int h = 5 + (int)(8 * (1.0f + sinf((float)now / 90.0f)));
      int r = h / 2;
      if (r > 8) r = 8;
      tft.fillRoundRect(cx - 18, cy - h / 2, 36, h, r, C_ACCENT);
      break;
    }
    case FACE_HAPPY:
      arc_dots(cx, cy - 2, 44, 0, 11, 2, C_IRIS);
      break;
    case FACE_SAD:
      arc_dots(cx, cy + 4, 36, 1, 7, 2, C_IRIS);
      break;
    case FACE_ERROR:
      tft.fillRect(cx - 16, cy - 3, 32, 6, C_BAD);
      break;
    case FACE_SLEEP:
      tft.fillRoundRect(cx - 12, cy - 2, 24, 4, 2, C_DIM);
      break;
    case FACE_IDLE:
    default:
      tft.fillRoundRect(cx - 19, cy - 2, 38, 5, 2, C_IRIS);
      break;
  }
}

static void draw_face_frame(uint32_t now) {
  tft.fillRect(0, FACE_AREA_Y, TFT_WIDTH_PX, FACE_AREA_H, C_BG);
  tft.fillRoundRect(14, 26, 212, 96, 20, C_PANEL);
  tft.drawRoundRect(14, 26, 212, 96, 20, C_BORDER);

  // status LED bar above the eyes
  uint16_t led = C_ACCENT2;
  if (g_face == FACE_THINK)  led = C_ACCENT;
  if (g_face == FACE_ERROR)  led = C_BAD;
  if (g_face == FACE_LISTEN) led = C_GOOD;
  if (g_face == FACE_SPEAK)  led = C_ACCENT;
  if (g_face == FACE_SLEEP)  led = C_DIM;
  int pulse = 20 + (int)(10.0f * sinf((float)now / 400.0f));
  tft.fillRoundRect(120 - pulse, 33, pulse * 2, 4, 2, led);

  // eyes
  const int lx = 86, rx = 154, ey = 70;
  int drift = (int)(3.0f * sinf((float)now / 1500.0f));

  switch (g_face) {
    case FACE_LISTEN:
      draw_eye_normal(lx, ey, 50, 54, false, 0, -4);
      draw_eye_normal(rx, ey, 50, 54, false, 0, -4);
      break;
    case FACE_THINK:
      draw_eye_normal(lx, ey, 44, 42, false, 4, -5);
      draw_eye_normal(rx, ey, 44, 42, false, 4, -5);
      break;
    case FACE_HAPPY:
      draw_eye_happy(lx, ey);
      draw_eye_happy(rx, ey);
      break;
    case FACE_SAD:
      draw_eye_normal(lx, ey + 2, 44, 30, false, 0, 4);
      draw_eye_normal(rx, ey + 2, 44, 30, false, 0, 4);
      break;
    case FACE_ERROR:
      draw_eye_cross(lx, ey);
      draw_eye_cross(rx, ey);
      break;
    case FACE_SLEEP:
      tft.fillRoundRect(lx - 22, ey - 2, 44, 5, 2, C_IRIS);
      tft.fillRoundRect(rx - 22, ey - 2, 44, 5, 2, C_IRIS);
      break;
    case FACE_SPEAK:
      draw_eye_normal(lx, ey, 44, 46, g_blinking, drift, 1);
      draw_eye_normal(rx, ey, 44, 46, g_blinking, drift, 1);
      break;
    case FACE_IDLE:
    default:
      draw_eye_normal(lx, ey, 44, 48, g_blinking, drift, 2);
      draw_eye_normal(rx, ey, 44, 48, g_blinking, drift, 2);
      break;
  }

  draw_mouth(g_face, now);

  if (g_face == FACE_SLEEP) {
    uint32_t cp = 'z';
    font_draw_cp(tft, 176, 42, cp, C_DIM, C_PANEL);
    font_draw_cp(tft, 190, 30, 'Z', C_DIM, C_PANEL);
  }
}

// ---------------------------------------------------------------------------
//  status bar + chat text
// ---------------------------------------------------------------------------
static void draw_status_bar() {
  tft.fillRect(0, 0, TFT_WIDTH_PX, STATUS_BAR_H, C_BG);

  char timebuf[16];
  time_t now = time(nullptr);
  if (now > 1600000000) {
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tmv);
  } else {
    strcpy(timebuf, "--:--:--");
  }

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(timebuf, 6, 6);

  String right;
  if (WiFi.status() == WL_CONNECTED) {
    right = WiFi.localIP().toString();
  } else {
    right = String("AP ") + WiFi.softAPIP().toString();
  }
  int w = right.length() * 6;
  if (w > 150) w = 150;
  tft.drawString(right, TFT_WIDTH_PX - 6 - w, 6);

  uint16_t dot = (WiFi.status() == WL_CONNECTED) ? C_GOOD : C_WARN;
  tft.fillCircle(112, 10, 3, dot);
  if (g_face == FACE_THINK) tft.fillCircle(122, 10, 3, C_ACCENT);
  else if (g_face == FACE_LISTEN) tft.fillCircle(122, 10, 3, C_GOOD);
  else tft.fillCircle(122, 10, 3, C_PANEL2);
}

static void draw_text_area() {
  tft.fillRect(0, TEXT_AREA_Y, TFT_WIDTH_PX, TEXT_AREA_H, C_BG);
  tft.drawFastHLine(0, TEXT_AREA_Y - 2, TFT_WIDTH_PX, C_PANEL2);

  String body = g_speech;
  if (body.length() == 0) {
    body = "從網頁輸入文字，或按麥克風說話。";
  }
  font_draw_text(tft, 6, TEXT_AREA_Y + 6, body,
                 body == g_speech && g_speech.length() ? C_TEXT : C_DIM,
                 C_BG, TFT_WIDTH_PX - 12, 6, nullptr);
}

// ---------------------------------------------------------------------------
//  public API
// ---------------------------------------------------------------------------
TFT_eSPI &display_tft() { return tft; }

bool display_begin() {
  Serial.println("[tft] init ST7789 240x240");
  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);
  tft.setTextWrap(false);
  palette();

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, 200);
#else
  ledcSetup(7, 5000, 8);
  ledcAttachPin(TFT_BL, 7);
  ledcWrite(7, 200);
#endif

  g_boot_ms = millis();
  g_next_blink = millis() + 3000;
  g_last_status = 0;
  g_speech = "";
  g_speech_dirty = true;
  g_face_dirty = true;
  draw_status_bar();
  return true;
}

void display_set_brightness(uint8_t value) {
  if (value < 5) value = 5;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(TFT_BL, value);
#else
  ledcWrite(7, value);
#endif
}

void display_apply_config() {
  AppConfig cfg;
  config_snapshot(cfg);
  tft.setRotation(cfg.rotation & 0x03);
  display_set_brightness((uint8_t)cfg.brightness);
  g_face_enabled = cfg.face_enabled;
  g_speech_dirty = true;
  g_face_dirty = true;
  g_last_status = 0;
}

void display_set_face(FaceExpression expr) {
  if (g_face == expr) return;
  g_face = expr;
  g_face_dirty = true;
  g_face_persistent = true;
  g_face_until = 0;
  if (expr == FACE_SPEAK) g_speak_until = millis() + 2500;
}

void display_set_speech(const String &text) {
  g_speech = text;
  g_speech.trim();
  g_speech_dirty = true;
  g_face_dirty = true;
  if (g_speech.length()) {
    uint32_t ms = 1800 + g_speech.length() * 70;
    if (ms > 9000) ms = 9000;
    g_face = FACE_SPEAK;
    g_face_persistent = false;
    g_face_until = 0;
    g_speak_until = millis() + ms;
  } else {
    g_face = FACE_IDLE;
    g_face_persistent = false;
    g_face_until = 0;
  }
}

void display_clear_speech() {
  g_speech = "";
  g_speech_dirty = true;
}

void display_toast(const String &text, uint16_t color) {
  g_toast = text;
  g_toast_color = color ? color : C_WARN;
  g_toast_until = millis() + 2600;
  g_toast_active = true;
  g_toast_dirty = true;
}

void display_boot_screen() {
  if (!bus_lock(1000)) return;
  tft.fillScreen(C_BG);
  tft.drawRoundRect(10, 10, 220, 220, 16, C_PANEL2);
  tft.drawRoundRect(11, 11, 218, 218, 16, C_PANEL);
  font_draw_text(tft, 24, 74, "AI 聊天助理", C_TEXT, C_BG, 200, 2);
  tft.setTextFont(2);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("TTGO T-Camera Plus", 46, 112);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("v" APP_VERSION, 100, 134);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("booting ...", 84, 168);
  bus_unlock();
}

void display_poll_job() {
  static uint32_t last_id = 0;
  static uint8_t  last_status = 255;

  JobSnapshot js;
  jobs_snapshot(js);

  if (js.status == JOB_STATUS_RUNNING) {
    if (js.kind == JOB_KIND_VOICE && js.progress < 55) {
      display_set_face(FACE_LISTEN);
    } else {
      display_set_face(FACE_THINK);
    }
    last_status = js.status;
    last_id = js.id;
    return;
  }

  if (js.status != last_status || js.id != last_id) {
    last_status = js.status;
    last_id = js.id;
    if (js.status == JOB_STATUS_DONE) {
      String show = js.result.length() ? js.result : js.transcript;
      if (show.length()) display_set_speech(show);
      else display_set_face(FACE_HAPPY);
    } else if (js.status == JOB_STATUS_FAILED) {
      display_toast(js.error.length() ? js.error : "發生錯誤", C_BAD);
      g_face = FACE_SAD;
      g_face_persistent = false;
      g_face_until = millis() + 4000;
    }
  }
}

FaceExpression display_face_from_name(const String &raw) {
  String name = raw;
  name.trim();
  name.toLowerCase();
  if (name == "listen" || name == "listening") return FACE_LISTEN;
  if (name == "think" || name == "thinking") return FACE_THINK;
  if (name == "speak" || name == "speaking") return FACE_SPEAK;
  if (name == "happy" || name == "smile") return FACE_HAPPY;
  if (name == "sad") return FACE_SAD;
  if (name == "error") return FACE_ERROR;
  if (name == "sleep" || name == "sleeping") return FACE_SLEEP;
  return FACE_IDLE;
}

const char *display_face_name(FaceExpression expr) {
  switch (expr) {
    case FACE_LISTEN: return "listen";
    case FACE_THINK:  return "think";
    case FACE_SPEAK:  return "speak";
    case FACE_HAPPY:  return "happy";
    case FACE_SAD:    return "sad";
    case FACE_ERROR:  return "error";
    case FACE_SLEEP:  return "sleep";
    case FACE_IDLE:
    default:          return "idle";
  }
}

FaceExpression display_current_face() { return g_face; }

void display_tick() {
  uint32_t now = millis();

  if (g_face_persistent == false && g_face_until && now > g_face_until) {
    g_face = FACE_IDLE;
    g_face_until = 0;
    g_face_dirty = true;
  }
  if (g_face == FACE_SPEAK && g_speak_until && now > g_speak_until) {
    g_face = FACE_IDLE;
    g_speak_until = 0;
    g_face_dirty = true;
  }

  if (g_blinking) {
    if (now - g_blink_at > 150) g_blinking = false;
  } else if (now > g_next_blink) {
    g_blinking = true;
    g_blink_at = now;
    g_next_blink = now + 2600 + (uint32_t)random(0, 3500);
  }

  if (!bus_lock(60)) return;
  tft.startWrite();

  if (now - g_last_status >= 1000) {
    g_last_status = now;
    draw_status_bar();
  }

  if (g_toast_active) {
    if (now >= g_toast_until) {
      g_toast_active = false;
      g_speech_dirty = true;
      g_face_dirty = true;
    } else {
      // The toast banner itself never changes, so draw it once per message
      // instead of repainting the whole face area on every loop iteration.
      if (g_toast_dirty) {
        g_toast_dirty = false;
        tft.fillRect(0, FACE_AREA_Y, TFT_WIDTH_PX, FACE_AREA_H, C_BG);
        tft.fillRoundRect(14, 38, 212, 68, 14, C_PANEL2);
        tft.drawRoundRect(14, 38, 212, 68, 14, g_toast_color);
        font_draw_text(tft, 26, 58, g_toast, g_toast_color, C_PANEL2, 188, 3, nullptr);
      }
      if (g_speech_dirty) {
        draw_text_area();
        g_speech_dirty = false;
      }
      tft.endWrite();
      bus_unlock();
      return;
    }
  }

  if (g_speech_dirty) {
    draw_text_area();
    g_speech_dirty = false;
  }

  if (g_face_enabled) {
    uint32_t interval = 100;
    if (g_face == FACE_LISTEN || g_face == FACE_SPEAK) interval = 70;
    else if (g_face == FACE_THINK) interval = 80;
    else if (g_face == FACE_SLEEP) interval = 300;
    else if (g_face == FACE_ERROR) interval = 150;

    if (now - g_last_frame >= interval) {
      g_last_frame = now;
      draw_face_frame(now);
    }
  } else if (g_face_dirty) {
    // Animations disabled: draw the face once, then leave it static.
    g_face_dirty = false;
    g_last_frame = now;
    draw_face_frame(now);
  }

  tft.endWrite();
  bus_unlock();
}
