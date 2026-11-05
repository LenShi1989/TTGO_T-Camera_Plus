// ============================================================================
//  font_tc.cpp - loads /font_tc16.bin from SPIFFS into PSRAM and renders it
//  with TFT_eSPI::drawBitmap().
//
//  File layout ("TCF2", little endian):
//      char[4] magic | u16 max_w | u16 max_h | u32 count | u32 stride
//      entry: u32 codepoint | u8 width | u8 height | u8 bitmap[32]
//  Bitmaps are row major, MSB first.  Narrow glyphs (width 8) use one byte
//  per row (first 16 bytes of the slot), wide glyphs use two bytes per row.
// ============================================================================
#include "font_tc.h"

#include <FS.h>
#include <SPIFFS.h>
#include <string.h>

#include "board_config.h"

static uint8_t *s_font       = nullptr;
static uint32_t s_count      = 0;
static uint32_t s_stride     = 0;
static uint16_t s_max_w      = 0;
static uint16_t s_max_h      = 0;
static bool     s_font_ok    = false;

static const uint8_t *find_glyph(uint32_t cp, uint8_t &width) {
  if (!s_font || s_count == 0) return nullptr;
  uint32_t lo = 0;
  uint32_t hi = s_count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    const uint8_t *entry = s_font + 16 + (size_t)mid * s_stride;
    uint32_t value;
    memcpy(&value, entry, 4);
    if (value == cp) {
      width = entry[4];
      return entry + 6;
    }
    if (value < cp) lo = mid + 1;
    else hi = mid;
  }
  return nullptr;
}

void font_unload() {
  if (s_font) {
    free(s_font);
    s_font = nullptr;
  }
  s_count = 0;
  s_font_ok = false;
}

bool font_load() {
  font_unload();
  if (!SPIFFS.begin(true)) {
    Serial.println("[font] SPIFFS unavailable");
    return false;
  }
  File f = SPIFFS.open(FONT_FILE, "r");
  if (!f) {
    Serial.printf("[font] %s not found in SPIFFS - upload the data folder!\n", FONT_FILE);
    return false;
  }

  uint8_t header[16];
  if (f.read(header, sizeof(header)) != sizeof(header)) {
    Serial.println("[font] short header");
    f.close();
    return false;
  }
  if (memcmp(header, "TCF2", 4) != 0) {
    Serial.println("[font] bad magic, regenerate with tools/make_font.py");
    f.close();
    return false;
  }
  uint16_t max_w, max_h;
  uint32_t count, stride;
  memcpy(&max_w, header + 4, 2);
  memcpy(&max_h, header + 6, 2);
  memcpy(&count, header + 8, 4);
  memcpy(&stride, header + 12, 4);

  size_t expected = 16 + (size_t)count * stride;
  if (count == 0 || count > 40000 || stride < 6 || stride > 64 || max_w > 32 || max_h > 32 ||
      f.size() < expected) {
    Serial.printf("[font] invalid header (count=%u stride=%u size=%u)\n",
                  (unsigned)count, (unsigned)stride, (unsigned)f.size());
    f.close();
    return false;
  }

  s_font = (uint8_t *)ps_malloc(expected);
  if (!s_font) s_font = (uint8_t *)malloc(expected);
  if (!s_font) {
    Serial.printf("[font] cannot allocate %u bytes\n", (unsigned)expected);
    f.close();
    return false;
  }

  memcpy(s_font, header, sizeof(header));
  size_t got = f.read(s_font + sizeof(header), expected - sizeof(header));
  f.close();
  if (got != expected - sizeof(header)) {
    Serial.printf("[font] read %u of %u bytes\n", (unsigned)got, (unsigned)(expected - 16));
    font_unload();
    return false;
  }

  s_count   = count;
  s_stride  = stride;
  s_max_w   = max_w;
  s_max_h   = max_h;
  s_font_ok = true;
  Serial.printf("[font] loaded %u glyphs, %u bytes\n", (unsigned)count, (unsigned)expected);
  return true;
}

bool font_available() { return s_font_ok; }

int font_utf8_next(const char *s, uint32_t &cp) {
  if (!s || !*s) {
    cp = 0;
    return 0;
  }
  uint8_t c = (uint8_t)s[0];
  if (c < 0x80) {
    cp = c;
    return 1;
  }
  int extra;
  if ((c & 0xE0) == 0xC0) {
    cp = c & 0x1F; extra = 1;
  } else if ((c & 0xF0) == 0xE0) {
    cp = c & 0x0F; extra = 2;
  } else if ((c & 0xF8) == 0xF0) {
    cp = c & 0x07; extra = 3;
  } else {
    cp = '?';
    return 1;
  }
  for (int i = 1; i <= extra; ++i) {
    uint8_t cc = (uint8_t)s[i];
    if ((cc & 0xC0) != 0x80) {
      cp = '?';
      return i;
    }
    cp = (cp << 6) | (cc & 0x3F);
  }
  return extra + 1;
}

uint8_t font_cp_width(uint32_t cp) {
  uint8_t w = 0;
  if (find_glyph(cp, w)) return w;
  return 0;
}

uint16_t font_draw_cp(TFT_eSPI &tft, int x, int y, uint32_t cp, uint16_t fg, uint16_t bg) {
  uint8_t w = 0;
  const uint8_t *bmp = find_glyph(cp, w);
  if (!bmp) return 0;
  tft.drawBitmap(x, y, bmp, w, s_max_h ? s_max_h : 16, fg, bg);
  return w;
}

int font_measure(const String &text) {
  int best = 0;
  int cur = 0;
  const char *s = text.c_str();
  while (*s) {
    uint32_t cp;
    int n = font_utf8_next(s, cp);
    if (n <= 0) break;
    s += n;
    if (cp == '\n' || cp == '\r') {
      if (cur > best) best = cur;
      cur = 0;
      continue;
    }
    if (cp == '\t') { cur += 16; continue; }
    uint8_t w = font_cp_width(cp);
    if (w) cur += w;
  }
  if (cur > best) best = cur;
  return best;
}

static int draw_fallback(TFT_eSPI &tft, int x, int y, const String &text,
                         uint16_t fg, uint16_t bg, int max_width, int max_lines,
                         bool *truncated) {
  tft.setTextFont(2);
  tft.setTextColor(fg, bg);
  tft.setTextDatum(TL_DATUM);
  int per_line = max_width / 8;
  if (per_line < 1) per_line = 1;
  int line = 0;
  int col = 0;
  for (size_t i = 0; i < text.length() && line < max_lines; ++i) {
    char c = text[i];
    if (c == '\r') continue;
    if (c == '\n') { line++; col = 0; continue; }
    if ((uint8_t)c >= 0x80) continue;
    if (col >= per_line) { line++; col = 0; if (line >= max_lines) { if (truncated) *truncated = true; break; } }
    tft.drawChar(x + col * 8, y + line * 16, c, fg, bg, 1);
    col++;
  }
  return line + 1;
}

int font_draw_text(TFT_eSPI &tft, int x, int y, const String &text,
                   uint16_t fg, uint16_t bg, int max_width, int max_lines,
                   bool *truncated) {
  if (truncated) *truncated = false;
  if (max_lines <= 0 || max_width <= 0) return 0;
  if (!s_font_ok) {
    return draw_fallback(tft, x, y, text, fg, bg, max_width, max_lines, truncated);
  }

  const char *s = text.c_str();
  int cx = x;
  int line = 0;

  while (*s && line < max_lines) {
    uint32_t cp;
    int n = font_utf8_next(s, cp);
    if (n <= 0) break;
    s += n;

    if (cp == '\r') continue;
    if (cp == '\n') {
      cx = x;
      line++;
      continue;
    }
    if (cp == '\t') {
      if (cx + 16 > x + max_width && cx > x) { cx = x; line++; }
      if (line < max_lines) cx += 16;
      continue;
    }

    uint8_t w = 0;
    const uint8_t *bmp = find_glyph(cp, w);
    if (!bmp) continue;   // emoji / characters outside Big5 are skipped

    if (cx + w > x + max_width && cx > x) {
      cx = x;
      line++;
      if (line >= max_lines) {
        if (truncated) *truncated = true;
        break;
      }
    }

    tft.drawBitmap(cx, y + line * FONT_LINE_HEIGHT, bmp, w,
                   s_max_h ? s_max_h : 16, fg, bg);
    cx += w;
  }

  if (*s && truncated) *truncated = true;
  return line + 1;
}
