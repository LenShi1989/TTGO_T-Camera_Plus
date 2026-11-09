// ============================================================================
//  display.h - ST7789 face animation + Traditional Chinese text screen
// ============================================================================
#ifndef TTGO_T_CAMERA_PLUS_DISPLAY_H
#define TTGO_T_CAMERA_PLUS_DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>

enum FaceExpression : uint8_t {
  FACE_IDLE = 0,
  FACE_LISTEN,
  FACE_THINK,
  FACE_SPEAK,
  FACE_HAPPY,
  FACE_SAD,
  FACE_ERROR,
  FACE_SLEEP
};

TFT_eSPI &display_tft();

bool display_begin();                  // tft.init() + backlight PWM
void display_apply_config();           // rotation / brightness / face from config
void display_tick();                   // call from loop(), never from a task

void display_set_face(FaceExpression expr);
void display_set_speech(const String &text);
void display_clear_speech();
void display_toast(const String &text, uint16_t color = 0);
void display_set_brightness(uint8_t value);
void display_boot_screen();
void display_poll_job();               // mirrors the AI worker state on screen

// Live camera viewfinder.  camera.cpp calls begin(), pushes RGB565 frames
// with frame(), then calls end() before capturing the JPEG.
void display_preview_begin();
void display_preview_end();
void display_preview_frame(const uint8_t *rgb565, uint16_t w, uint16_t h, int remain);

FaceExpression display_face_from_name(const String &name);
const char    *display_face_name(FaceExpression expr);
FaceExpression display_current_face();

#endif  // TTGO_T_CAMERA_PLUS_DISPLAY_H
