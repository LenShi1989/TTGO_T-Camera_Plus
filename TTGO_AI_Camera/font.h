#pragma once
#include <Arduino.h>

// SD 卡字型檔格式（little-endian）：
//   magic "TGF1" | u16 version | u16 width | u16 height | u16 flags
//   u32 count | u32 index_offset | u32 data_offset
//   index[count]: u32 codepoint, u32 glyph_offset（相對 data_offset）
//   data: 每個字 32 bytes（16 列 × 2 bytes，MSB 在左）
class Font {
 public:
  bool begin(const String &path);
  void end();
  bool ok() const { return _base != nullptr; }

  // 回傳 32 bytes 點陣；找不到時回傳 nullptr
  const uint8_t *glyph(uint32_t cp) const;
  // 該字元的顯示寬度（像素，未套用縮放）
  uint8_t advance(uint32_t cp) const;
  bool isWide(uint32_t cp) const { return cp >= 0x2E80; }

  uint16_t glyphHeight() const { return _h; }
  uint16_t glyphWidth() const { return _w; }
  uint32_t count() const { return _count; }
  const String &path() const { return _path; }

 private:
  uint8_t *_base = nullptr;
  uint32_t _size = 0;
  uint32_t _count = 0;
  uint32_t _indexOff = 0;
  uint32_t _dataOff = 0;
  uint16_t _w = 16;
  uint16_t _h = 16;
  String _path;
};

extern Font g_font;
