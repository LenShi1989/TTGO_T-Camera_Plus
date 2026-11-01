#pragma once
#include <Arduino.h>

// 顏色以「已位元組交換」的 RGB565 儲存，可直接送進 ST7789
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  return (uint16_t)((c >> 8) | (c << 8));
}

#define COL_BG rgb(8, 10, 18)
#define COL_PANEL rgb(20, 24, 36)
#define COL_PANEL2 rgb(30, 36, 52)
#define COL_ACCENT rgb(0, 200, 255)
#define COL_ACCENT2 rgb(0, 120, 200)
#define COL_TEXT rgb(236, 241, 250)
#define COL_DIM rgb(132, 144, 168)
#define COL_GREEN rgb(0, 220, 120)
#define COL_RED rgb(255, 72, 72)
#define COL_YELLOW rgb(255, 205, 60)
#define COL_ORANGE rgb(255, 140, 40)
#define COL_PURPLE rgb(170, 100, 255)
#define COL_BLUE rgb(70, 130, 255)
#define COL_WHITE rgb(255, 255, 255)
#define COL_BLACK rgb(0, 0, 0)
#define COL_CYAN rgb(0, 230, 230)

class Display {
 public:
  bool begin();
  void setBrightness(uint8_t value);

  uint16_t *fb() { return _fb; }
  int width() const { return 240; }
  int height() const { return 240; }

  void clear(uint16_t color);
  void drawPixel(int x, int y, uint16_t color);
  void fillRect(int x, int y, int w, int h, uint16_t color);
  void drawRect(int x, int y, int w, int h, uint16_t color);
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color);
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color);
  void drawHLine(int x, int y, int w, uint16_t color);
  void drawVLine(int x, int y, int h, uint16_t color);
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color);
  void drawCircle(int cx, int cy, int r, uint16_t color);
  void fillCircle(int cx, int cy, int r, uint16_t color);
  void fillCircleAlpha(int cx, int cy, int r, uint16_t color, uint8_t alpha);

  int charWidth(uint32_t cp, int scale = 1) const;
  int textWidth(const char *s, int scale = 1) const;

  int drawChar(int x, int y, uint32_t cp, uint16_t fg, uint16_t bg, bool opaque, int scale);
  int drawText(int x, int y, const char *s, uint16_t fg, int scale = 1);
  int drawTextBg(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale = 1);
  void drawTextCenter(int y, const char *s, uint16_t fg, int scale = 1);
  int drawTextWrapped(int x, int y, int maxW, const char *s, uint16_t fg, uint16_t bg,
                      bool opaque, int scale, int lineHeight, int maxLines);

  // 從相機 RGB565 緩衝區複製一塊到螢幕（srcW/srcH 為來源區塊尺寸，srcStride 為實際行寬）
  void blitRGB565(int dx, int dy, int dw, int dh, const uint16_t *src, int srcStride, int srcW,
                  int srcH, int srcX, int srcY);

  void setWindow(int x, int y, int w, int h);
  void push();
  void pushRect(int x, int y, int w, int h);

 private:
  uint16_t *_fb = nullptr;
};

extern Display g_disp;
