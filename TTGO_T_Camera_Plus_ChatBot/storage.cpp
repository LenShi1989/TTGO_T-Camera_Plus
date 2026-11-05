// ============================================================================
//  storage.cpp - SPIFFS + SD card access
// ============================================================================
#include "storage.h"

#include <FS.h>
#include <SPIFFS.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <algorithm>
#include <strings.h>
#include <vector>

#include "board_config.h"

static SemaphoreHandle_t s_bus_mutex = nullptr;
static bool              s_spiffs_ok = false;
static bool              s_sd_ok     = false;

// ---------------------------------------------------------------------------
//  bus lock
// ---------------------------------------------------------------------------
bool bus_lock(uint32_t timeout_ms) {
  if (!s_bus_mutex) return true;
  return xSemaphoreTakeRecursive(s_bus_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void bus_unlock() {
  if (s_bus_mutex) xSemaphoreGiveRecursive(s_bus_mutex);
}

bool sd_ready()     { return s_sd_ok; }
bool spiffs_ready() { return s_spiffs_ok; }

// ---------------------------------------------------------------------------
//  boot
// ---------------------------------------------------------------------------
bool storage_begin() {
  if (!s_bus_mutex) s_bus_mutex = xSemaphoreCreateRecursiveMutex();

  Serial.println("[fs] mounting SPIFFS ...");
  s_spiffs_ok = SPIFFS.begin(true);
  if (s_spiffs_ok) {
    Serial.printf("[fs] SPIFFS ok  %u / %u bytes used\n",
                  (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
  } else {
    Serial.println("[fs] SPIFFS mount FAILED");
  }

  // The SD card shares the panel's SPI bus.  display_begin() already called
  // tft.init(), which started the bus on the right pins.
  pinMode(BOARD_SD_CS, OUTPUT);
  digitalWrite(BOARD_SD_CS, HIGH);
  delay(20);

  SPIClass &spi = TFT_eSPI::getSPIinstance();
  for (int attempt = 1; attempt <= 3 && !s_sd_ok; ++attempt) {
    Serial.printf("[fs] mounting SD card (attempt %d) ...\n", attempt);
    s_sd_ok = SD.begin(BOARD_SD_CS, spi, SD_SPI_FREQUENCY, SD_MOUNT_POINT, 8);
    if (!s_sd_ok) delay(150);
  }

  if (s_sd_ok) {
    Serial.printf("[fs] SD ok  %s  %llu / %llu bytes used\n", SD_MOUNT_POINT,
                  (unsigned long long)SD.usedBytes(),
                  (unsigned long long)SD.totalBytes());
    String err;
    if (!sd_ensure_dir(VOICE_DIR, err)) {
      Serial.printf("[fs] cannot create %s: %s\n", VOICE_DIR, err.c_str());
    }
  } else {
    Serial.println("[fs] SD mount FAILED - running without a card");
  }
  return s_sd_ok;
}

// ---------------------------------------------------------------------------
//  path helpers
// ---------------------------------------------------------------------------
bool sd_norm(const String &raw, String &out) {
  String in = raw;
  in.replace("\\", "/");
  in.trim();
  if (in.length() == 0) in = "/";
  if (!in.startsWith("/")) in = "/" + in;

  String norm;
  int i = 0;
  while (i < (int)in.length()) {
    int j = in.indexOf('/', i);
    if (j < 0) j = in.length();
    String seg = in.substring(i, j);
    i = j + 1;
    if (seg.length() == 0 || seg == ".") continue;
    if (seg == "..") return false;
    if (seg.indexOf(':') >= 0) return false;
    if (norm.length() + seg.length() + 2 > MAX_PATH_LEN) return false;
    norm += "/";
    norm += seg;
  }
  if (norm.length() == 0) norm = "/";
  out = norm;
  return true;
}

String sd_abs(const String &normalized) {
  // The SD library mounts the card at "/sd" and its VFS layer prepends that
  // mount point itself, so paths handed to SD.open()/SD.mkdir() must NOT
  // include it.
  return normalized;
}

static String parent_of(const String &norm) {
  int slash = norm.lastIndexOf('/');
  if (slash <= 0) return "/";
  return norm.substring(0, slash);
}

String json_escape(const String &s) {
  String o;
  o.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t c = (uint8_t)s[i];
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\r': o += "\\r";  break;
      case '\t': o += "\\t";  break;
      default:
        if (c < 0x20) {
          char tmp[8];
          snprintf(tmp, sizeof(tmp), "\\u%04x", c);
          o += tmp;
        } else {
          o += (char)c;
        }
    }
  }
  return o;
}

String u64_to_string(uint64_t value) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  return String(buf);
}

// ---------------------------------------------------------------------------
//  directory helpers
// ---------------------------------------------------------------------------
bool sd_ensure_dir(const String &path, String &err) {
  String norm;
  if (!sd_norm(path, norm)) {
    err = "路徑不合法";
    return false;
  }
  if (norm == "/") return true;
  if (!sd_ready()) {
    err = "SD 卡未掛載";
    return false;
  }

  if (!bus_lock()) {
    err = "SD 卡忙碌中";
    return false;
  }

  bool ok = true;
  int idx = 1;
  while (true) {
    int slash = norm.indexOf('/', idx);
    String part = (slash < 0) ? norm : norm.substring(0, slash);
    String fs_path = sd_abs(part);
    if (!SD.exists(fs_path)) {
      if (!SD.mkdir(fs_path)) {
        err = "無法建立資料夾 " + part;
        ok = false;
        break;
      }
    }
    if (slash < 0) break;
    idx = slash + 1;
  }
  bus_unlock();
  return ok;
}

static bool remove_recursive(const String &norm, int depth, String &err) {
  if (depth > 12) {
    err = "資料夾層級過深";
    return false;
  }
  String fs_path = sd_abs(norm);
  File f = SD.open(fs_path);
  if (!f) {
    err = "無法開啟 " + norm;
    return false;
  }

  if (!f.isDirectory()) {
    f.close();
    if (!SD.remove(fs_path)) {
      err = "無法刪除 " + norm;
      return false;
    }
    return true;
  }

  std::vector<String> children;
  File child = f.openNextFile();
  while (child) {
    String name = child.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name.length() && name != "." && name != "..") {
      children.push_back(norm == "/" ? "/" + name : norm + "/" + name);
    }
    child = f.openNextFile();
  }
  f.close();

  for (auto &c : children) {
    if (!remove_recursive(c, depth + 1, err)) return false;
  }

  if (!SD.rmdir(fs_path)) {
    err = "無法刪除資料夾 " + norm;
    return false;
  }
  return true;
}

bool sd_remove(const String &path, String &err) {
  String norm;
  if (!sd_norm(path, norm)) { err = "路徑不合法"; return false; }
  if (norm == "/") { err = "不能刪除根目錄"; return false; }
  if (!sd_ready()) { err = "SD 卡未掛載"; return false; }
  if (!bus_lock()) { err = "SD 卡忙碌中"; return false; }
  bool ok = remove_recursive(norm, 0, err);
  bus_unlock();
  return ok;
}

bool sd_rename(const String &from, const String &to, String &err) {
  String a, b;
  if (!sd_norm(from, a) || !sd_norm(to, b)) { err = "路徑不合法"; return false; }
  if (a == "/" || b == "/") { err = "不能移動根目錄"; return false; }
  if (b.startsWith(a + "/")) { err = "不能把資料夾移入自己"; return false; }
  if (!sd_ready()) { err = "SD 卡未掛載"; return false; }
  if (!bus_lock()) { err = "SD 卡忙碌中"; return false; }

  bool ok = true;
  if (SD.exists(sd_abs(b))) {
    err = "目標已存在";
    ok = false;
  } else {
    String parent = parent_of(b);
    String derr;
    if (parent != "/" && !sd_ensure_dir(parent, derr)) {
      err = derr;
      ok = false;
    } else if (!SD.rename(sd_abs(a), sd_abs(b))) {
      err = "重新命名失敗";
      ok = false;
    }
  }
  bus_unlock();
  return ok;
}

bool sd_read_text(const String &path, size_t max_bytes, String &out, String &err) {
  String norm;
  if (!sd_norm(path, norm)) { err = "路徑不合法"; return false; }
  if (!sd_ready()) { err = "SD 卡未掛載"; return false; }
  if (!bus_lock()) { err = "SD 卡忙碌中"; return false; }

  String fs_path = sd_abs(norm);
  bool ok = true;
  out = "";
  File f = SD.open(fs_path, "r");
  if (!f || f.isDirectory()) {
    err = "無法讀取檔案";
    ok = false;
  } else {
    size_t size = f.size();
    if (size > max_bytes) size = max_bytes;
    out.reserve(size + 1);
    while (f.available() && out.length() < max_bytes) {
      out += (char)f.read();
    }
    f.close();
  }
  bus_unlock();
  return ok;
}

bool sd_write_text(const String &path, const String &content, String &err) {
  String norm;
  if (!sd_norm(path, norm)) { err = "路徑不合法"; return false; }
  if (!sd_ready()) { err = "SD 卡未掛載"; return false; }

  String parent = parent_of(norm);
  if (parent != "/" && !sd_ensure_dir(parent, err)) return false;

  if (!bus_lock()) { err = "SD 卡忙碌中"; return false; }
  String fs_path = sd_abs(norm);
  File f = SD.open(fs_path, "w");
  bool ok = true;
  if (!f) {
    err = "無法寫入檔案";
    ok = false;
  } else {
    size_t written = f.print(content);
    f.close();
    if (written != content.length()) {
      err = "寫入不完整";
      ok = false;
    }
  }
  bus_unlock();
  return ok;
}

// ---------------------------------------------------------------------------
//  listing
// ---------------------------------------------------------------------------
struct DirEntry {
  String name;
  String path;
  bool   dir;
  uint64_t size;
  time_t mtime;
};

static void format_time(time_t t, char *buf, size_t len) {
  buf[0] = 0;
  if (t < 1600000000) return;
  struct tm tmv;
  if (!localtime_r(&t, &tmv)) return;
  strftime(buf, len, "%Y-%m-%d %H:%M", &tmv);
}

bool sd_list_json(const String &path, String &out, String &err) {
  String norm;
  if (!sd_norm(path, norm)) { err = "路徑不合法"; return false; }
  if (!sd_ready()) { err = "SD 卡未掛載"; return false; }
  if (!bus_lock()) { err = "SD 卡忙碌中"; return false; }

  String fs_path = sd_abs(norm);
  File dir = SD.open(fs_path);
  if (!dir || !dir.isDirectory()) {
    err = "資料夾不存在";
    bus_unlock();
    return false;
  }

  std::vector<DirEntry> entries;
  entries.reserve(32);
  bool truncated = false;

  File f = dir.openNextFile();
  while (f) {
    String name = f.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name.length() && name != "." && name != "..") {
      if (entries.size() >= SD_LIST_MAX_ENTRIES) {
        truncated = true;
      } else {
        DirEntry e;
        e.name = name;
        e.path = (norm == "/") ? "/" + name : norm + "/" + name;
        e.dir  = f.isDirectory();
        e.size = e.dir ? 0 : (uint64_t)f.size();
        e.mtime = f.getLastWrite();
        entries.push_back(e);
      }
    }
    f = dir.openNextFile();
  }
  dir.close();

  uint64_t total = SD.totalBytes();
  uint64_t used  = SD.usedBytes();
  bus_unlock();

  std::sort(entries.begin(), entries.end(), [](const DirEntry &a, const DirEntry &b) {
    if (a.dir != b.dir) return a.dir > b.dir;
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });

  out.reserve(256 + entries.size() * 96);
  out  = "{\"ok\":true,\"path\":\"";
  out += json_escape(norm);
  out += "\",\"parent\":\"";
  out += json_escape(parent_of(norm));
  out += "\",\"total\":";
  out += u64_to_string(total);
  out += ",\"used\":";
  out += u64_to_string(used);
  out += ",\"truncated\":";
  out += truncated ? "true" : "false";
  out += ",\"entries\":[";
  for (size_t i = 0; i < entries.size(); ++i) {
    if (i) out += ',';
    char tbuf[24];
    format_time(entries[i].mtime, tbuf, sizeof(tbuf));
    out += "{\"name\":\"";
    out += json_escape(entries[i].name);
    out += "\",\"path\":\"";
    out += json_escape(entries[i].path);
    out += "\",\"dir\":";
    out += entries[i].dir ? "true" : "false";
    out += ",\"size\":";
    out += u64_to_string(entries[i].size);
    out += ",\"mtime\":\"";
    out += tbuf;
    out += "\"}";
  }
  out += "]}";
  return true;
}

static void list_spiffs_dir(File dir, const String &prefix, String &out, bool &first, int depth) {
  if (depth > 4) return;
  File f = dir.openNextFile();
  while (f) {
    String name = f.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name.length() && name != "." && name != "..") {
      String full = prefix + "/" + name;
      if (f.isDirectory()) {
        list_spiffs_dir(f, full, out, first, depth + 1);
      } else {
        if (!first) out += ',';
        first = false;
        out += "{\"name\":\"";
        out += json_escape(name);
        out += "\",\"path\":\"";
        out += json_escape(full);
        out += "\",\"size\":";
        out += u64_to_string((uint64_t)f.size());
        out += "}";
      }
    }
    f = dir.openNextFile();
  }
}

bool spiffs_list_json(String &out) {
  if (!s_spiffs_ok) return false;
  File root = SPIFFS.open("/");
  if (!root || !root.isDirectory()) return false;

  out = "{\"ok\":true,\"total\":";
  out += u64_to_string((uint64_t)SPIFFS.totalBytes());
  out += ",\"used\":";
  out += u64_to_string((uint64_t)SPIFFS.usedBytes());
  out += ",\"files\":[";
  bool first = true;
  list_spiffs_dir(root, "", out, first, 0);
  out += "]}";
  return true;
}
