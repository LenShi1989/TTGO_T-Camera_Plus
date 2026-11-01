#pragma once
#include <Arduino.h>

// UTF-8 工具
uint32_t utf8Decode(const char *s, size_t len, size_t &i);
String utf8Encode(uint32_t cp);
size_t utf8Count(const String &s);
String utf8Truncate(const String &s, size_t maxBytes);

String urlEncode(const String &s);
String htmlEscape(const String &s);
String jsonEscape(const String &s);

size_t base64EncodedLength(size_t len);
size_t base64Encode(const uint8_t *src, size_t len, char *dst);

size_t wavHeader(uint8_t *dst, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample,
                 uint32_t dataBytes);

String fmtString(const char *fmt, ...);
void logLine(const char *tag, const char *fmt, ...);

String trimCopy(const String &s);
String floatToString(float v, uint8_t decimals);

// 16 進位字串 <-> 位元組
String hexEncode(const uint8_t *data, size_t len);
size_t hexDecode(const char *hex, uint8_t *out, size_t maxLen);
