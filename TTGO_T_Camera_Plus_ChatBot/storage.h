// ============================================================================
//  storage.h - SPIFFS assets + SD card filesystem helpers
// ============================================================================
#ifndef TTGO_T_CAMERA_PLUS_STORAGE_H
#define TTGO_T_CAMERA_PLUS_STORAGE_H

#include <Arduino.h>

// --- boot ------------------------------------------------------------------
bool storage_begin();          // mount SPIFFS, then the SD card
bool sd_ready();
bool spiffs_ready();

// --- SPI / SD mutual exclusion --------------------------------------------
//  The ST7789 panel and the SD card share one SPI bus.  Every piece of code
//  that talks to either device must hold this lock.  It is recursive, so
//  nested helpers are safe.
bool bus_lock(uint32_t timeout_ms = 4000);
void bus_unlock();

// --- SD helpers (paths are always relative to the card root, "/" = root) ---
bool   sd_norm(const String &raw, String &out);
String sd_abs(const String &normalized);
bool   sd_ensure_dir(const String &path, String &err);
bool   sd_read_text(const String &path, size_t max_bytes, String &out, String &err);
bool   sd_write_text(const String &path, const String &content, String &err);
bool   sd_remove(const String &path, String &err);
bool   sd_rename(const String &from, const String &to, String &err);
bool   sd_list_json(const String &path, String &out, String &err);
bool   spiffs_list_json(String &out);

// --- misc -----------------------------------------------------------------
String json_escape(const String &s);
String u64_to_string(uint64_t value);

#endif  // TTGO_T_CAMERA_PLUS_STORAGE_H
