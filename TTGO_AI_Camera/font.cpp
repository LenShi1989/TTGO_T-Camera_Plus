#include "font.h"

#include <SD.h>
#include <string.h>

#include "util.h"

Font g_font;

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void Font::end() {
  if (_base) {
    free(_base);
    _base = nullptr;
  }
  _count = 0;
}

bool Font::begin(const String &path) {
  end();
  _path = path;

  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) {
    logLine("FONT", "找不到字型檔 %s", path.c_str());
    return false;
  }
  size_t size = f.size();
  if (size < 32) {
    logLine("FONT", "字型檔太小 (%u bytes)", (unsigned)size);
    f.close();
    return false;
  }

  _base = (uint8_t *)ps_malloc(size);
  if (!_base) _base = (uint8_t *)malloc(size);
  if (!_base) {
    logLine("FONT", "記憶體不足，無法載入字型 (%u bytes)", (unsigned)size);
    f.close();
    return false;
  }

  size_t got = 0;
  while (got < size) {
    size_t want = size - got;
    if (want > 4096) want = 4096;
    int r = f.read(_base + got, want);
    if (r <= 0) break;
    got += (size_t)r;
  }
  f.close();
  if (got != size) {
    logLine("FONT", "字型讀取不完整 (%u/%u)", (unsigned)got, (unsigned)size);
    end();
    return false;
  }

  if (memcmp(_base, "TGF1", 4) != 0) {
    logLine("FONT", "字型檔格式錯誤");
    end();
    return false;
  }
  _size = (uint32_t)size;
  _w = rd16(_base + 6);
  _h = rd16(_base + 8);
  _count = rd32(_base + 12);
  _indexOff = rd32(_base + 16);
  _dataOff = rd32(_base + 20);
  if (_w == 0 || _h == 0 || _indexOff + _count * 8 > _size || _dataOff > _size) {
    logLine("FONT", "字型檔標頭異常");
    end();
    return false;
  }
  logLine("FONT", "字型載入完成：%u 個字，%u bytes", (unsigned)_count, (unsigned)size);
  return true;
}

const uint8_t *Font::glyph(uint32_t cp) const {
  if (!_base || _count == 0) return nullptr;
  uint32_t lo = 0, hi = _count;
  const uint8_t *index = _base + _indexOff;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    uint32_t v = rd32(index + mid * 8);
    if (v == cp) {
      uint32_t off = rd32(index + mid * 8 + 4);
      if (_dataOff + off + 32 <= _size) {
        return _base + _dataOff + off;
      }
      return nullptr;
    }
    if (v < cp)
      lo = mid + 1;
    else
      hi = mid;
  }
  return nullptr;
}

uint8_t Font::advance(uint32_t cp) const { return isWide(cp) ? _w : (_w / 2); }
