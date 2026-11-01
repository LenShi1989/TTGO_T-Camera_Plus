#include "display.h"

#include <SPI.h>
#include <string.h>

#include "board_pins.h"
#include "font.h"
#include "storage.h"
#include "util.h"

Display g_disp;

#define W 240
#define H 240

// ---------------------------------------------------------------- 5x7 備援字型
// 當 SD 卡字型載入失敗時，至少還能顯示 ASCII。
static const uint8_t kFont5x7[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x07, 0x00, 0x07,
    0x00, 0x14, 0x7F, 0x14, 0x7F, 0x14, 0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x23, 0x13, 0x08,
    0x64, 0x62, 0x36, 0x49, 0x55, 0x22, 0x50, 0x00, 0x05, 0x03, 0x00, 0x00, 0x00, 0x1C,
    0x22, 0x41, 0x00, 0x00, 0x41, 0x22, 0x1C, 0x00, 0x08, 0x2A, 0x1C, 0x2A, 0x08, 0x08,
    0x08, 0x3E, 0x08, 0x08, 0x00, 0x50, 0x30, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x00, 0x60, 0x60, 0x00, 0x00, 0x20, 0x10, 0x08, 0x04, 0x02, 0x3E, 0x51, 0x49, 0x45,
    0x3E, 0x00, 0x42, 0x7F, 0x40, 0x00, 0x42, 0x61, 0x51, 0x49, 0x46, 0x21, 0x41, 0x45,
    0x4B, 0x31, 0x18, 0x14, 0x12, 0x7F, 0x10, 0x27, 0x45, 0x45, 0x45, 0x39, 0x3C, 0x4A,
    0x49, 0x49, 0x30, 0x01, 0x71, 0x09, 0x05, 0x03, 0x36, 0x49, 0x49, 0x49, 0x36, 0x06,
    0x49, 0x49, 0x29, 0x1E, 0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x56, 0x36, 0x00, 0x00,
    0x08, 0x14, 0x22, 0x41, 0x00, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x41, 0x22, 0x14,
    0x08, 0x02, 0x01, 0x51, 0x09, 0x06, 0x32, 0x49, 0x79, 0x41, 0x3E, 0x7E, 0x11, 0x11,
    0x11, 0x7E, 0x7F, 0x49, 0x49, 0x49, 0x36, 0x3E, 0x41, 0x41, 0x41, 0x22, 0x7F, 0x41,
    0x41, 0x22, 0x1C, 0x7F, 0x49, 0x49, 0x49, 0x41, 0x7F, 0x09, 0x09, 0x09, 0x01, 0x3E,
    0x41, 0x49, 0x49, 0x7A, 0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x41, 0x7F, 0x41, 0x00,
    0x20, 0x40, 0x41, 0x3F, 0x01, 0x7F, 0x08, 0x14, 0x22, 0x41, 0x7F, 0x40, 0x40, 0x40,
    0x40, 0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x7F, 0x04, 0x08, 0x10, 0x7F, 0x3E, 0x41, 0x41,
    0x41, 0x3E, 0x7F, 0x09, 0x09, 0x09, 0x06, 0x3E, 0x41, 0x51, 0x21, 0x5E, 0x7F, 0x09,
    0x19, 0x29, 0x46, 0x46, 0x49, 0x49, 0x49, 0x31, 0x01, 0x01, 0x7F, 0x01, 0x01, 0x3F,
    0x40, 0x40, 0x40, 0x3F, 0x1F, 0x20, 0x40, 0x20, 0x1F, 0x3F, 0x40, 0x38, 0x40, 0x3F,
    0x63, 0x14, 0x08, 0x14, 0x63, 0x07, 0x08, 0x70, 0x08, 0x07, 0x61, 0x51, 0x49, 0x45,
    0x43, 0x00, 0x7F, 0x41, 0x41, 0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x41, 0x41,
    0x7F, 0x00, 0x04, 0x02, 0x01, 0x02, 0x04, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x01,
    0x02, 0x04, 0x00, 0x20, 0x54, 0x54, 0x54, 0x78, 0x7F, 0x48, 0x44, 0x44, 0x38, 0x38,
    0x44, 0x44, 0x44, 0x20, 0x38, 0x44, 0x44, 0x48, 0x7F, 0x38, 0x54, 0x54, 0x54, 0x18,
    0x08, 0x7E, 0x09, 0x01, 0x02, 0x0C, 0x52, 0x52, 0x52, 0x3E, 0x7F, 0x08, 0x04, 0x04,
    0x78, 0x00, 0x44, 0x7D, 0x40, 0x00, 0x20, 0x40, 0x44, 0x3D, 0x00, 0x7F, 0x10, 0x28,
    0x44, 0x00, 0x00, 0x41, 0x7F, 0x40, 0x00, 0x7C, 0x04, 0x18, 0x04, 0x78, 0x7C, 0x08,
    0x04, 0x04, 0x78, 0x38, 0x44, 0x44, 0x44, 0x38, 0x7C, 0x14, 0x14, 0x14, 0x08, 0x08,
    0x14, 0x14, 0x18, 0x7C, 0x7C, 0x08, 0x04, 0x04, 0x08, 0x48, 0x54, 0x54, 0x54, 0x20,
    0x04, 0x3F, 0x44, 0x40, 0x20, 0x3C, 0x40, 0x40, 0x20, 0x7C, 0x1C, 0x20, 0x40, 0x20,
    0x1C, 0x3C, 0x40, 0x30, 0x40, 0x3C, 0x44, 0x28, 0x10, 0x28, 0x44, 0x0C, 0x50, 0x50,
    0x50, 0x3C, 0x44, 0x64, 0x54, 0x4C, 0x44, 0x00, 0x08, 0x36, 0x41, 0x00, 0x00, 0x00,
    0x7F, 0x00, 0x00, 0x00, 0x41, 0x36, 0x08, 0x00, 0x08, 0x04, 0x08, 0x10, 0x08,
};

// ---------------------------------------------------------------- ST7789 初始化
struct LcdCmd {
  uint8_t cmd;
  uint8_t data[16];
  uint8_t len;
  uint16_t delay_ms;
};

static const LcdCmd kSt7789Init[] = {
    {0xC0, {0x00}, 1, 0},
    {0xC2, {0x01, 0xFF}, 2, 0},
    {0xC3, {0x13}, 1, 0},
    {0xC4, {0x20}, 1, 0},
    {0xC6, {0x0F}, 1, 0},
    {0xCA, {0x0F}, 1, 0},
    {0xC8, {0x08}, 1, 0},
    {0x55, {0xB0}, 1, 0},
    {0x36, {0x00}, 1, 0},
    {0x3A, {0x55}, 1, 0},
    {0xB1, {0x40, 0x02, 0x14}, 3, 0},
    {0x26, {0x01}, 1, 0},
    {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4, 0},
    {0x2B, {0x00, 0x00, 0x01, 0x3F}, 4, 0},
    {0x2C, {0x00}, 1, 0},
    {0xE0, {0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37, 0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19}, 14, 0},
    {0xE1, {0xD0, 0x00, 0x05, 0x0D, 0x0C, 0x06, 0x2D, 0x44, 0x40, 0x0E, 0x1C, 0x18, 0x16, 0x19}, 14, 0},
    {0x11, {0}, 0, 120},
    {0x29, {0}, 0, 20},
};

static inline void csLow() { digitalWrite(PIN_TFT_CS, LOW); }
static inline void csHigh() { digitalWrite(PIN_TFT_CS, HIGH); }
static inline void dcCmd() { digitalWrite(PIN_TFT_DC, LOW); }
static inline void dcData() { digitalWrite(PIN_TFT_DC, HIGH); }

static void lcdWriteCmd(uint8_t cmd, const uint8_t *data, size_t len) {
  dcCmd();
  csLow();
  spiBus.write(cmd);
  if (len) {
    dcData();
    spiBus.writeBytes(data, len);
  }
  csHigh();
}

// ---------------------------------------------------------------- 基本繪圖

bool Display::begin() {
  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_TFT_DC, OUTPUT);
  pinMode(PIN_TFT_BL, OUTPUT);
  csHigh();
  dcData();
  digitalWrite(PIN_TFT_BL, LOW);

  _fb = (uint16_t *)ps_malloc((size_t)W * H * 2);
  if (!_fb) _fb = (uint16_t *)malloc((size_t)W * H * 2);
  if (!_fb) {
    logLine("LCD", "無法配置顯示緩衝區");
    return false;
  }
  memset(_fb, 0, (size_t)W * H * 2);

  spiBus.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  for (size_t i = 0; i < sizeof(kSt7789Init) / sizeof(kSt7789Init[0]); i++) {
    lcdWriteCmd(kSt7789Init[i].cmd, kSt7789Init[i].data, kSt7789Init[i].len);
    if (kSt7789Init[i].delay_ms) delay(kSt7789Init[i].delay_ms);
  }
  spiBus.endTransaction();

  setBrightness(255);
  clear(COL_BLACK);
  push();
  logLine("LCD", "ST7789 初始化完成，緩衝區 %u bytes", (unsigned)((size_t)W * H * 2));
  return true;
}

void Display::setBrightness(uint8_t value) {
  static bool inited = false;
  if (!inited) {
    ledcSetup(LEDC_TIMER_TFT_BL, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, LEDC_CH_TFT_BL);
    inited = true;
  }
  ledcWrite(LEDC_CH_TFT_BL, value);
}

void Display::clear(uint16_t color) {
  uint32_t *p = (uint32_t *)_fb;
  uint32_t v = ((uint32_t)color << 16) | color;
  for (int i = 0; i < W * H / 2; i++) p[i] = v;
}

void Display::drawPixel(int x, int y, uint16_t color) {
  if (x < 0 || y < 0 || x >= W || y >= H) return;
  _fb[y * W + x] = color;
}

void Display::fillRect(int x, int y, int w, int h, uint16_t color) {
  if (w <= 0 || h <= 0) return;
  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = x + w > W ? W : x + w;
  int y1 = y + h > H ? H : y + h;
  if (x0 >= x1 || y0 >= y1) return;
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *row = _fb + yy * W;
    for (int xx = x0; xx < x1; xx++) row[xx] = color;
  }
}

void Display::drawRect(int x, int y, int w, int h, uint16_t color) {
  drawHLine(x, y, w, color);
  drawHLine(x, y + h - 1, w, color);
  drawVLine(x, y, h, color);
  drawVLine(x + w - 1, y, h, color);
}

void Display::fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
  if (r <= 0) {
    fillRect(x, y, w, h, color);
    return;
  }
  fillRect(x + r, y, w - 2 * r, h, color);
  fillRect(x, y + r, r, h - 2 * r, color);
  fillRect(x + w - r, y + r, r, h - 2 * r, color);
  fillCircle(x + r, y + r, r, color);
  fillCircle(x + w - r - 1, y + r, r, color);
  fillCircle(x + r, y + h - r - 1, r, color);
  fillCircle(x + w - r - 1, y + h - r - 1, r, color);
}

void Display::drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
  drawHLine(x + r, y, w - 2 * r, color);
  drawHLine(x + r, y + h - 1, w - 2 * r, color);
  drawVLine(x, y + r, h - 2 * r, color);
  drawVLine(x + w - 1, y + r, h - 2 * r, color);
  // 四角以圓弧近似
  for (int i = 0; i <= r; i++) {
    int dy = r - i;
    int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    drawPixel(x + r - dx, y + i, color);
    drawPixel(x + w - 1 - r + dx, y + i, color);
    drawPixel(x + r - dx, y + h - 1 - i, color);
    drawPixel(x + w - 1 - r + dx, y + h - 1 - i, color);
  }
}

void Display::drawHLine(int x, int y, int w, uint16_t color) {
  if (y < 0 || y >= H) return;
  int x0 = x < 0 ? 0 : x;
  int x1 = x + w > W ? W : x + w;
  for (int xx = x0; xx < x1; xx++) _fb[y * W + xx] = color;
}

void Display::drawVLine(int x, int y, int h, uint16_t color) {
  if (x < 0 || x >= W) return;
  int y0 = y < 0 ? 0 : y;
  int y1 = y + h > H ? H : y + h;
  for (int yy = y0; yy < y1; yy++) _fb[yy * W + x] = color;
}

void Display::drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    drawPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void Display::drawCircle(int cx, int cy, int r, uint16_t color) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    drawPixel(cx + x, cy + y, color);
    drawPixel(cx + y, cy + x, color);
    drawPixel(cx - y, cy + x, color);
    drawPixel(cx - x, cy + y, color);
    drawPixel(cx - x, cy - y, color);
    drawPixel(cx - y, cy - x, color);
    drawPixel(cx + y, cy - x, color);
    drawPixel(cx + x, cy - y, color);
    y++;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}

void Display::fillCircle(int cx, int cy, int r, uint16_t color) {
  if (r <= 0) return;
  for (int y = -r; y <= r; y++) {
    int dx = (int)(sqrtf((float)(r * r - y * y)) + 0.5f);
    drawHLine(cx - dx, cy + y, dx * 2 + 1, color);
  }
}

void Display::fillCircleAlpha(int cx, int cy, int r, uint16_t color, uint8_t alpha) {
  if (r <= 0) return;
  // 拆解來源與目的顏色（皆為已交換格式）
  uint16_t sc = (uint16_t)((color >> 8) | (color << 8));
  uint8_t sr = (sc >> 11) & 0x1F, sg = (sc >> 5) & 0x3F, sb = sc & 0x1F;
  for (int y = -r; y <= r; y++) {
    int dx = (int)(sqrtf((float)(r * r - y * y)) + 0.5f);
    int py = cy + y;
    if (py < 0 || py >= H) continue;
    for (int x = -dx; x <= dx; x++) {
      int px = cx + x;
      if (px < 0 || px >= W) continue;
      uint16_t d = _fb[py * W + px];
      uint16_t dc = (uint16_t)((d >> 8) | (d << 8));
      uint8_t dr = (dc >> 11) & 0x1F, dg = (dc >> 5) & 0x3F, db = dc & 0x1F;
      uint8_t rr = (uint8_t)((sr * alpha + dr * (255 - alpha)) / 255);
      uint8_t rg = (uint8_t)((sg * alpha + dg * (255 - alpha)) / 255);
      uint8_t rb = (uint8_t)((sb * alpha + db * (255 - alpha)) / 255);
      uint16_t out = (uint16_t)((rr << 11) | (rg << 5) | rb);
      _fb[py * W + px] = (uint16_t)((out >> 8) | (out << 8));
    }
  }
}

// ---------------------------------------------------------------- 文字

int Display::charWidth(uint32_t cp, int scale) const {
  if (g_font.ok()) return g_font.advance(cp) * scale;
  return (cp < 0x2E80 ? 6 : 12) * scale;
}

int Display::textWidth(const char *s, int scale) const {
  if (!s) return 0;
  size_t len = strlen(s);
  size_t i = 0;
  int w = 0;
  while (i < len) {
    uint32_t cp = utf8Decode(s, len, i);
    if (cp == '\n') break;
    w += charWidth(cp, scale);
  }
  return w;
}

static int drawFallbackChar(Display *d, uint16_t *fb, int x, int y, uint32_t cp, uint16_t fg,
                            uint16_t bg, bool opaque, int scale) {
  (void)d;
  if (cp < 0x20 || cp > 0x7E) cp = '?';
  const uint8_t *g = kFont5x7 + (cp - 0x20) * 5;
  for (int col = 0; col < 5; col++) {
    uint8_t bits = g[col];
    for (int row = 0; row < 7; row++) {
      bool on = (bits >> row) & 1;
      if (!on && !opaque) continue;
      uint16_t c = on ? fg : bg;
      for (int sy = 0; sy < scale; sy++) {
        for (int sx = 0; sx < scale; sx++) {
          int px = x + col * scale + sx;
          int py = y + row * scale + sy;
          if (px >= 0 && px < 240 && py >= 0 && py < 240) fb[py * 240 + px] = c;
        }
      }
    }
  }
  return x + 6 * scale;
}

int Display::drawChar(int x, int y, uint32_t cp, uint16_t fg, uint16_t bg, bool opaque, int scale) {
  if (scale < 1) scale = 1;
  const uint8_t *g = g_font.ok() ? g_font.glyph(cp) : nullptr;
  if (!g) {
    return drawFallbackChar(this, _fb, x, y, cp, fg, bg, opaque, scale);
  }
  bool wide = g_font.isWide(cp);
  int gw = wide ? 16 : 8;
  for (int row = 0; row < 16; row++) {
    uint16_t bits = (uint16_t)(((uint16_t)g[row * 2] << 8) | g[row * 2 + 1]);
    for (int col = 0; col < gw; col++) {
      bool on = (bits >> (15 - col)) & 1;
      if (!on && !opaque) continue;
      uint16_t c = on ? fg : bg;
      if (scale == 1) {
        int px = x + col, py = y + row;
        if (px >= 0 && px < W && py >= 0 && py < H) _fb[py * W + px] = c;
      } else {
        fillRect(x + col * scale, y + row * scale, scale, scale, c);
      }
    }
  }
  return x + gw * scale;
}

int Display::drawText(int x, int y, const char *s, uint16_t fg, int scale) {
  return drawTextWrapped(x, y, W - x, s, fg, COL_BLACK, false, scale, 16 * scale + 2, 1);
}

int Display::drawTextBg(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
  return drawTextWrapped(x, y, W - x, s, fg, bg, true, scale, 16 * scale + 2, 1);
}

void Display::drawTextCenter(int y, const char *s, uint16_t fg, int scale) {
  int w = textWidth(s, scale);
  int x = (W - w) / 2;
  if (x < 0) x = 0;
  drawText(x, y, s, fg, scale);
}

int Display::drawTextWrapped(int x, int y, int maxW, const char *s, uint16_t fg, uint16_t bg,
                             bool opaque, int scale, int lineHeight, int maxLines) {
  if (!s) return 0;
  size_t len = strlen(s);
  size_t i = 0;
  int cx = x, cy = y;
  int lines = 1;
  while (i < len && lines <= maxLines) {
    size_t start = i;
    uint32_t cp = utf8Decode(s, len, i);
    if (cp == '\r') continue;
    if (cp == '\n') {
      cx = x;
      cy += lineHeight;
      lines++;
      continue;
    }
    int cw = charWidth(cp, scale);
    if (cx + cw > x + maxW && cx > x) {
      cx = x;
      cy += lineHeight;
      lines++;
      if (lines > maxLines) break;
    }
    char tmp[8];
    size_t n = i - start;
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, s + start, n);
    tmp[n] = 0;
    cx = drawChar(cx, cy, cp, fg, bg, opaque, scale);
    if (cx >= W) break;
  }
  return lines;
}

// ---------------------------------------------------------------- 影像

void Display::blitRGB565(int dx, int dy, int dw, int dh, const uint16_t *src, int srcStride,
                         int srcW, int srcH, int srcX, int srcY) {
  for (int y = 0; y < dh; y++) {
    int sy = srcY + y * srcH / dh;
    if (sy < 0 || sy >= srcH) continue;
    int py = dy + y;
    if (py < 0 || py >= H) continue;
    const uint16_t *srow = src + sy * srcStride;
    uint16_t *drow = _fb + py * W;
    for (int x = 0; x < dw; x++) {
      int sx = srcX + x * srcW / dw;
      if (sx < 0 || sx >= srcW) continue;
      int px = dx + x;
      if (px < 0 || px >= W) continue;
      uint16_t c = srow[sx];
      drow[px] = (uint16_t)((c >> 8) | (c << 8));
    }
  }
}

// ---------------------------------------------------------------- 輸出

void Display::setWindow(int x, int y, int w, int h) {
  uint8_t buf[4];
  uint16_t x1 = (uint16_t)(x + w - 1);
  uint16_t y1 = (uint16_t)(y + h - 1);
  buf[0] = (uint8_t)(x >> 8);
  buf[1] = (uint8_t)(x & 0xFF);
  buf[2] = (uint8_t)(x1 >> 8);
  buf[3] = (uint8_t)(x1 & 0xFF);
  lcdWriteCmd(0x2A, buf, 4);
  buf[0] = (uint8_t)(y >> 8);
  buf[1] = (uint8_t)(y & 0xFF);
  buf[2] = (uint8_t)(y1 >> 8);
  buf[3] = (uint8_t)(y1 & 0xFF);
  lcdWriteCmd(0x2B, buf, 4);
  lcdWriteCmd(0x2C, nullptr, 0);
}

void Display::push() {
  if (!_fb) return;
  spiBus.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  setWindow(0, 0, W, H);
  dcData();
  csLow();
  const uint8_t *p = (const uint8_t *)_fb;
  size_t remaining = (size_t)W * H * 2;
  while (remaining) {
    size_t chunk = remaining > 8192 ? 8192 : remaining;
    spiBus.writeBytes(p, chunk);
    p += chunk;
    remaining -= chunk;
  }
  csHigh();
  spiBus.endTransaction();
}

void Display::pushRect(int x, int y, int w, int h) {
  if (!_fb || w <= 0 || h <= 0) return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;

  spiBus.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  setWindow(x, y, w, h);
  dcData();
  csLow();
  for (int row = 0; row < h; row++) {
    const uint8_t *p = (const uint8_t *)(_fb + (y + row) * W + x);
    size_t remaining = (size_t)w * 2;
    while (remaining) {
      size_t chunk = remaining > 4096 ? 4096 : remaining;
      spiBus.writeBytes(p, chunk);
      p += chunk;
      remaining -= chunk;
    }
  }
  csHigh();
  spiBus.endTransaction();
}
