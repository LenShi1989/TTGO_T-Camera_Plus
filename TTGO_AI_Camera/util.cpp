#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

uint32_t utf8Decode(const char *s, size_t len, size_t &i) {
  if (i >= len) return 0;
  uint8_t c = (uint8_t)s[i];
  if (c < 0x80) {
    i++;
    return c;
  }
  uint32_t cp = 0;
  uint8_t extra = 0;
  if ((c & 0xE0) == 0xC0) {
    cp = c & 0x1F;
    extra = 1;
  } else if ((c & 0xF0) == 0xE0) {
    cp = c & 0x0F;
    extra = 2;
  } else if ((c & 0xF8) == 0xF0) {
    cp = c & 0x07;
    extra = 3;
  } else {
    i++;
    return 0xFFFD;
  }
  if (i + extra >= len + 1) {
    i++;
    return 0xFFFD;
  }
  for (uint8_t k = 1; k <= extra; k++) {
    if (i + k >= len) {
      i++;
      return 0xFFFD;
    }
    uint8_t cc = (uint8_t)s[i + k];
    if ((cc & 0xC0) != 0x80) {
      i++;
      return 0xFFFD;
    }
    cp = (cp << 6) | (cc & 0x3F);
  }
  i += extra + 1;
  return cp;
}

String utf8Encode(uint32_t cp) {
  char out[5];
  size_t n = 0;
  if (cp < 0x80) {
    out[n++] = (char)cp;
  } else if (cp < 0x800) {
    out[n++] = (char)(0xC0 | (cp >> 6));
    out[n++] = (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out[n++] = (char)(0xE0 | (cp >> 12));
    out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[n++] = (char)(0x80 | (cp & 0x3F));
  } else {
    out[n++] = (char)(0xF0 | (cp >> 18));
    out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[n++] = (char)(0x80 | (cp & 0x3F));
  }
  out[n] = 0;
  return String(out);
}

size_t utf8Count(const String &s) {
  size_t i = 0, n = 0;
  while (i < s.length()) {
    utf8Decode(s.c_str(), s.length(), i);
    n++;
  }
  return n;
}

String utf8Truncate(const String &s, size_t maxBytes) {
  if (s.length() <= maxBytes) return s;
  size_t i = 0, last = 0;
  while (i < s.length() && i < maxBytes) {
    last = i;
    utf8Decode(s.c_str(), s.length(), i);
  }
  return s.substring(0, last);
}

String urlEncode(const String &s) {
  static const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String htmlEscape(const String &s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

size_t base64EncodedLength(size_t len) { return 4 * ((len + 2) / 3); }

size_t base64Encode(const uint8_t *src, size_t len, char *dst) {
  static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0, i = 0;
  while (i + 2 < len) {
    uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) | src[i + 2];
    dst[o++] = tbl[(v >> 18) & 0x3F];
    dst[o++] = tbl[(v >> 12) & 0x3F];
    dst[o++] = tbl[(v >> 6) & 0x3F];
    dst[o++] = tbl[v & 0x3F];
    i += 3;
  }
  size_t rem = len - i;
  if (rem == 1) {
    uint32_t v = (uint32_t)src[i] << 16;
    dst[o++] = tbl[(v >> 18) & 0x3F];
    dst[o++] = tbl[(v >> 12) & 0x3F];
    dst[o++] = '=';
    dst[o++] = '=';
  } else if (rem == 2) {
    uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8);
    dst[o++] = tbl[(v >> 18) & 0x3F];
    dst[o++] = tbl[(v >> 12) & 0x3F];
    dst[o++] = tbl[(v >> 6) & 0x3F];
    dst[o++] = '=';
  }
  dst[o] = 0;
  return o;
}

size_t wavHeader(uint8_t *dst, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample,
                 uint32_t dataBytes) {
  uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
  uint16_t blockAlign = channels * bitsPerSample / 8;
  uint32_t chunkSize = 36 + dataBytes;
  memcpy(dst, "RIFF", 4);
  memcpy(dst + 4, &chunkSize, 4);
  memcpy(dst + 8, "WAVE", 4);
  memcpy(dst + 12, "fmt ", 4);
  uint32_t sub1 = 16;
  memcpy(dst + 16, &sub1, 4);
  uint16_t fmt = 1;
  memcpy(dst + 20, &fmt, 2);
  memcpy(dst + 22, &channels, 2);
  memcpy(dst + 24, &sampleRate, 4);
  memcpy(dst + 28, &byteRate, 4);
  memcpy(dst + 32, &blockAlign, 2);
  memcpy(dst + 34, &bitsPerSample, 2);
  memcpy(dst + 36, "data", 4);
  memcpy(dst + 40, &dataBytes, 4);
  return 44;
}

String fmtString(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return String(buf);
}

void logLine(const char *tag, const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.printf("[%s] %s\n", tag, buf);
}

String trimCopy(const String &s) {
  int a = 0, b = (int)s.length() - 1;
  while (a <= b && isspace((unsigned char)s[a])) a++;
  while (b >= a && isspace((unsigned char)s[b])) b--;
  if (b < a) return String();
  return s.substring(a, b + 1);
}

String floatToString(float v, uint8_t decimals) {
  return String(v, (unsigned int)decimals);
}

String hexEncode(const uint8_t *data, size_t len) {
  static const char *hex = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    out += hex[data[i] >> 4];
    out += hex[data[i] & 0x0F];
  }
  return out;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

size_t hexDecode(const char *hex, uint8_t *out, size_t maxLen) {
  size_t n = 0;
  while (hex[0] && hex[1] && n < maxLen) {
    int hi = hexVal(hex[0]);
    int lo = hexVal(hex[1]);
    if (hi < 0 || lo < 0) break;
    out[n++] = (uint8_t)((hi << 4) | lo);
    hex += 2;
  }
  return n;
}
