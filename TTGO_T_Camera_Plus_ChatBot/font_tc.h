// ============================================================================
//  font_tc.h - 16x16 Traditional Chinese bitmap font (SPIFFS + PSRAM)
// ============================================================================
#ifndef TTGO_T_CAMERA_PLUS_FONT_TC_H
#define TTGO_T_CAMERA_PLUS_FONT_TC_H

#include <Arduino.h>
#include <TFT_eSPI.h>

#define FONT_LINE_HEIGHT 17

bool font_load();
bool font_available();
void font_unload();

int  font_utf8_next(const char *s, uint32_t &cp);   // returns bytes consumed
uint8_t font_cp_width(uint32_t cp);                // 0 = glyph not available
uint16_t font_draw_cp(TFT_eSPI &tft, int x, int y, uint32_t cp,
                      uint16_t fg, uint16_t bg);
int  font_measure(const String &text);              // width of the longest line
int  font_draw_text(TFT_eSPI &tft, int x, int y, const String &text,
                    uint16_t fg, uint16_t bg, int max_width, int max_lines,
                    bool *truncated = nullptr);

#endif  // TTGO_T_CAMERA_PLUS_FONT_TC_H
