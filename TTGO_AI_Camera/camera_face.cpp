#include "camera_face.h"

#include <SD.h>
#include <math.h>
#include <string.h>

#include "app_config.h"
#include "board_pins.h"
#include "display.h"
#include "esp_camera.h"
#include "fb_gfx.h"
#include "fd_forward.h"
#include "fr_forward.h"
#include "img_converters.h"
#include "storage.h"
#include "util.h"

#define FACE_VEC_SIZE 512
#define FACE_NAME_MAX 24

// 網頁 / TFT 串流最大只用 QVGA：再大不但 fps 掉，人臉偵測也會變慢。
#define CAM_STREAM_MAX_FS FRAMESIZE_QVGA

struct FaceEntry {
  char name[FACE_NAME_MAX];
  float *vec;
};

static bool s_camReady = false;
static uint32_t s_nextInitTry = 0;  // 初始化失敗後的冷卻時間
static uint32_t s_lastReinit = 0;   // 上次重新初始化時間
static uint8_t s_reinitCount = 0;   // 連續重新初始化次數
static uint32_t s_grabErrors = 0;   // 取樣失敗次數（診斷用）
static uint32_t s_fps = 0;
static uint32_t s_fpsFrames = 0;
static uint32_t s_fpsStamp = 0;
static uint32_t s_lastBytes = 0;

static FaceEntry *s_faces = nullptr;
static int s_faceCount = 0;
static int s_faceCap = 0;

static uint8_t *s_rgb = nullptr;
static int s_rgbW = 0, s_rgbH = 0;
static dl_matrix3du_t *s_aligned = nullptr;
static uint16_t *s_565 = nullptr;
static bool s_rgbFresh = false; // s_rgb 內是否為目前這張影格的解碼結果

// ---------------------------------------------------------------- 相機

bool camBegin() {
  if (s_camReady) return true;

  // 初始化失敗後先冷卻 5 秒，避免每個 HTTP 請求都卡在 1~2 秒的重試上。
  uint32_t now = millis();
  if (s_nextInitTry && (int32_t)(now - s_nextInitTry) < 0) return false;

  int fs = g_cfg.camera.framesize;
  if (fs < 0) fs = 0;
  if (fs > CAM_STREAM_MAX_FS) fs = CAM_STREAM_MAX_FS;
  int quality = g_cfg.camera.jpeg_quality;
  if (quality < 8) quality = 8;
  if (quality > 30) quality = 30;

  camera_config_t cfg = {};
  cfg.pin_pwdn = CAM_PIN_PWDN;
  cfg.pin_reset = CAM_PIN_RESET;
  cfg.pin_xclk = CAM_PIN_XCLK;
  cfg.pin_sscb_sda = CAM_PIN_SIOD;
  cfg.pin_sscb_scl = CAM_PIN_SIOC;
  cfg.pin_d7 = CAM_PIN_D7;
  cfg.pin_d6 = CAM_PIN_D6;
  cfg.pin_d5 = CAM_PIN_D5;
  cfg.pin_d4 = CAM_PIN_D4;
  cfg.pin_d3 = CAM_PIN_D3;
  cfg.pin_d2 = CAM_PIN_D2;
  cfg.pin_d1 = CAM_PIN_D1;
  cfg.pin_d0 = CAM_PIN_D0;
  cfg.pin_vsync = CAM_PIN_VSYNC;
  cfg.pin_href = CAM_PIN_HREF;
  cfg.pin_pclk = CAM_PIN_PCLK;
  cfg.xclk_freq_hz = g_cfg.camera.xclk_hz;
  cfg.ledc_timer = LEDC_TIMER_0;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  // 用 JPEG 串流：單張只有十幾 KB，網頁即時影像比 RGB565 轉檔快很多；
  // 人臉偵測與 TFT 畫面再用 fmt2rgb888 解碼。
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size = (framesize_t)fs;
  cfg.jpeg_quality = quality;
  cfg.fb_count = 2;

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    // 少數模組在 JPEG 模式下無法配置雙緩衝，退回單緩衝再試一次。
    // 先 deinit 清掉失敗時殘留的 DMA / PSRAM，否則第二次 init 可能直接失敗。
    logLine("CAM", "雙緩衝初始化失敗 (0x%x)，改用單緩衝重試", (unsigned)err);
    esp_camera_deinit();
    cfg.fb_count = 1;
    err = esp_camera_init(&cfg);
  }
  if (err != ESP_OK) {
    s_nextInitTry = millis() + 5000;
    logLine("CAM", "相機初始化失敗 (0x%x)，5 秒後再試（請檢查鏡頭排線 / PSRAM）",
            (unsigned)err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, (framesize_t)fs);
    s->set_quality(s, quality);
    if (g_cfg.camera.vflip) s->set_vflip(s, 1);
    if (g_cfg.camera.hmirror) s->set_hmirror(s, 1);
  }

  s_camReady = true;
  s_nextInitTry = 0;
  s_reinitCount = 0;
  s_fps = 0;
  s_fpsFrames = 0;
  s_fpsStamp = millis();
  s_lastBytes = 0;
  logLine("CAM", "OV2640 就緒：JPEG %ux%u q=%d fb=%u", resolution[fs].width,
          resolution[fs].height, quality, (unsigned)cfg.fb_count);
  return true;
}

// 取一張影格；失敗時重新初始化相機再試一次（供電不穩或相機卡住時可自癒）。
// 重新初始化有節流：連續失敗 3 次或 3 秒內只做一次，避免拖垮整個網頁。
static camera_fb_t *grabFrame() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb) {
    s_reinitCount = 0;
    return fb;
  }

  s_grabErrors++;
  if (s_grabErrors % 20 == 1) {
    logLine("CAM", "取樣失敗（累計 %u 次，heap %u / PSRAM %u）", (unsigned)s_grabErrors,
            (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  }
  if (!s_camReady) return nullptr;
  if (s_reinitCount >= 3) return nullptr;

  uint32_t now = millis();
  if (s_lastReinit && (uint32_t)(now - s_lastReinit) < 3000) return nullptr;
  s_lastReinit = now;
  s_reinitCount++;

  logLine("CAM", "重新初始化相機（第 %u 次）", (unsigned)s_reinitCount);
  camEnd();
  if (!camBegin()) return nullptr;
  fb = esp_camera_fb_get();
  if (!fb) s_grabErrors++;
  return fb;
}

bool camReady() { return s_camReady; }

uint32_t camFps() { return s_fps; }
uint32_t camLastFrameBytes() { return s_lastBytes; }
uint32_t camErrorCount() { return s_grabErrors; }

void camEnd() {
  if (!s_camReady) return;
  esp_camera_deinit();
  s_camReady = false;
  s_fps = 0;
  s_fpsFrames = 0;
}

static bool ensureRgbBuffer(int w, int h) {
  if (s_rgb && s_rgbW == w && s_rgbH == h) return true;
  if (s_rgb) {
    free(s_rgb);
    s_rgb = nullptr;
  }
  size_t need = (size_t)w * h * 3;
  s_rgb = (uint8_t *)ps_malloc(need);
  if (!s_rgb) s_rgb = (uint8_t *)malloc(need);
  if (!s_rgb) {
    logLine("CAM", "RGB888 緩衝區配置失敗 (%u bytes)", (unsigned)need);
    return false;
  }
  s_rgbW = w;
  s_rgbH = h;
  s_rgbFresh = false;
  return true;
}

// 把一張影格置中裁成 240x240 顯示到 TFT。
// decoded=true 表示呼叫端已經先把 JPEG 解到 s_rgb（人臉偵測流程），
// 可以省掉一次 JPEG 解碼。
static bool displayFrame(camera_fb_t *fb, bool decoded) {
  if (!fb) return false;
  int w = fb->width, h = fb->height;
  int side = w < h ? w : h;
  int cropX = (w - side) / 2;

  if (fb->format == PIXFORMAT_RGB565) {
    g_disp.blitRGB565(0, 0, 240, 240, (const uint16_t *)fb->buf, w, side, side, cropX, 0);
    return true;
  }

  if (!decoded || !s_rgbFresh) {
    if (!ensureRgbBuffer(w, h)) return false;
    s_rgbFresh = false;
    if (!fmt2rgb888(fb->buf, fb->len, fb->format, s_rgb)) return false;
    s_rgbFresh = true;
  }
  if (!s_565) s_565 = (uint16_t *)ps_malloc(240 * 240 * sizeof(uint16_t));
  if (!s_565) return false;

  // 置中裁成 240x240，再轉成 RGB565（相機格式為小端位元組）
  for (int y = 0; y < 240; y++) {
    int sy = y * side / 240;
    const uint8_t *row = s_rgb + (size_t)sy * w * 3;
    uint16_t *out = s_565 + y * 240;
    for (int x = 0; x < 240; x++) {
      const uint8_t *p = row + (size_t)(cropX + x * side / 240) * 3;
      out[x] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
    }
  }
  g_disp.blitRGB565(0, 0, 240, 240, s_565, 240, 240, 240, 0, 0);
  return true;
}

camera_fb_t *camGrabFrame() {
  camera_fb_t *fb = grabFrame();
  if (!fb) return nullptr;

  uint32_t now = millis();
  s_lastBytes = fb->len;
  s_fpsFrames++;
  if (s_fpsStamp == 0) s_fpsStamp = now;
  uint32_t dt = now - s_fpsStamp;
  if (dt >= 5000) {
    s_fps = (uint32_t)((uint64_t)s_fpsFrames * 1000ULL / dt);
    s_fpsFrames = 0;
    s_fpsStamp = now;
    logLine("CAM", "串流 %u fps，影格 %u bytes，heap %u KB / PSRAM %u KB",
            (unsigned)s_fps, (unsigned)s_lastBytes, (unsigned)(ESP.getFreeHeap() / 1024),
            (unsigned)(ESP.getFreePsram() / 1024));
  }
  return fb;
}

void camReturnFrame(camera_fb_t *fb) {
  if (fb) esp_camera_fb_return(fb);
}

bool camPreviewToDisplay() {
  camera_fb_t *fb = camGrabFrame();
  if (!fb) return false;
  bool ok = displayFrame(fb, false);
  camReturnFrame(fb);
  return ok;
}

bool camSnapshotJpeg(uint8_t **out, size_t *outLen) {
  *out = nullptr;
  *outLen = 0;
  if (!s_camReady) return false;
  camera_fb_t *fb = camGrabFrame();
  if (!fb) return false;

  bool ok = false;
  if (fb->format == PIXFORMAT_JPEG) {
    uint8_t *buf = (uint8_t *)ps_malloc(fb->len);
    if (!buf) buf = (uint8_t *)malloc(fb->len);
    if (buf) {
      memcpy(buf, fb->buf, fb->len);
      *out = buf;
      *outLen = fb->len;
      ok = true;
    }
  } else {
    ok = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format,
                 (uint8_t)g_cfg.camera.jpeg_quality, out, outLen);
  }

  camReturnFrame(fb);
  return ok;
}

// ---------------------------------------------------------------- 人臉資料

static bool ensureFaceArray() {
  if (s_faces) return true;
  s_faceCap = g_cfg.camera.max_faces;
  if (s_faceCap < 1) s_faceCap = 1;
  s_faces = (FaceEntry *)ps_calloc(s_faceCap, sizeof(FaceEntry));
  if (!s_faces) s_faces = (FaceEntry *)calloc(s_faceCap, sizeof(FaceEntry));
  return s_faces != nullptr;
}

static void freeFaceVec(int idx) {
  if (idx < 0 || idx >= s_faceCount) return;
  if (s_faces[idx].vec) {
    free(s_faces[idx].vec);
    s_faces[idx].vec = nullptr;
  }
}

static void clearFaces() {
  for (int i = 0; i < s_faceCount; i++) freeFaceVec(i);
  s_faceCount = 0;
}

int faceCount() { return s_faceCount; }

String faceNameAt(int idx) {
  if (idx < 0 || idx >= s_faceCount) return String();
  return String(s_faces[idx].name);
}

static String sanitizeName(const String &name) {
  String out;
  for (size_t i = 0; i < name.length() && out.length() < FACE_NAME_MAX - 1; i++) {
    char c = name[i];
    if (c == ',' || c == '"' || c == '\n' || c == '\r') continue;
    out += c;
  }
  if (out.isEmpty()) out = "ID";
  return out;
}

bool faceLoad() {
  if (!ensureFaceArray()) return false;
  clearFaces();
  if (!storageExists(g_cfg.faces_path)) {
    logLine("FACE", "尚無人臉資料檔 (%s)", g_cfg.faces_path.c_str());
    return true;
  }

  File f = SD.open(g_cfg.faces_path.c_str(), FILE_READ);
  if (!f) return false;

  while (f.available() && s_faceCount < s_faceCap) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;
    if (line.startsWith("id,")) continue; // 標頭

    int c1 = line.indexOf(',');
    if (c1 < 0) continue;
    int c2 = line.indexOf(',', c1 + 1);
    if (c2 < 0) continue;

    String name = sanitizeName(line.substring(c1 + 1, c2));
    float *vec = (float *)ps_malloc(FACE_VEC_SIZE * sizeof(float));
    if (!vec) vec = (float *)malloc(FACE_VEC_SIZE * sizeof(float));
    if (!vec) break;

    const char *p = line.c_str() + c2 + 1;
    int got = 0;
    while (got < FACE_VEC_SIZE && *p) {
      char *end = nullptr;
      float v = strtof(p, &end);
      if (end == p) break;
      vec[got++] = v;
      p = end;
      if (*p == ',') p++;
    }
    if (got != FACE_VEC_SIZE) {
      free(vec);
      continue;
    }
    strncpy(s_faces[s_faceCount].name, name.c_str(), FACE_NAME_MAX - 1);
    s_faces[s_faceCount].name[FACE_NAME_MAX - 1] = 0;
    s_faces[s_faceCount].vec = vec;
    s_faceCount++;
  }
  f.close();
  logLine("FACE", "已載入 %d 個人臉 ID", s_faceCount);
  return true;
}

bool faceSave() {
  if (!storageReady()) return false;
  File f = SD.open(g_cfg.faces_path.c_str(), FILE_WRITE);
  if (!f) {
    logLine("FACE", "無法寫入 %s", g_cfg.faces_path.c_str());
    return false;
  }
  f.print("id,name");
  for (int i = 0; i < FACE_VEC_SIZE; i++) {
    f.print(",v");
    f.print(i);
  }
  f.print("\n");

  for (int i = 0; i < s_faceCount; i++) {
    f.print(i);
    f.print(",");
    f.print(s_faces[i].name);
    for (int k = 0; k < FACE_VEC_SIZE; k++) {
      f.print(",");
      f.print(s_faces[i].vec[k], 6);
    }
    f.print("\n");
  }
  f.close();
  logLine("FACE", "已儲存 %d 個人臉 ID 到 %s", s_faceCount, g_cfg.faces_path.c_str());
  return true;
}

bool faceExportCsv(String &msg) {
  if (!storageReady()) {
    msg = "SD 卡未就緒";
    return false;
  }
  if (!faceSave()) {
    msg = "寫入 CSV 失敗";
    return false;
  }
  File f = SD.open(g_cfg.faces_path.c_str(), FILE_READ);
  size_t size = f ? f.size() : 0;
  if (f) f.close();
  msg = String("已匯出 ") + s_faceCount + " 筆到 " + g_cfg.faces_path + "（" + size + " bytes）";
  return true;
}

bool faceRemoveAt(int idx) {
  if (idx < 0 || idx >= s_faceCount) return false;
  freeFaceVec(idx);
  for (int i = idx; i + 1 < s_faceCount; i++) {
    s_faces[i] = s_faces[i + 1];
  }
  s_faceCount--;
  memset(&s_faces[s_faceCount], 0, sizeof(FaceEntry));
  faceSave();
  return true;
}

bool faceRemove(const String &name) {
  for (int i = 0; i < s_faceCount; i++) {
    if (name == s_faces[i].name) return faceRemoveAt(i);
  }
  return false;
}

bool faceRename(int idx, const String &newName) {
  if (idx < 0 || idx >= s_faceCount) return false;
  String n = sanitizeName(newName);
  strncpy(s_faces[idx].name, n.c_str(), FACE_NAME_MAX - 1);
  s_faces[idx].name[FACE_NAME_MAX - 1] = 0;
  return faceSave();
}

// ---------------------------------------------------------------- 偵測 / 辨識

static float cosineSimilarity(const float *a, const float *b) {
  double dot = 0, na = 0, nb = 0;
  for (int i = 0; i < FACE_VEC_SIZE; i++) {
    dot += (double)a[i] * b[i];
    na += (double)a[i] * a[i];
    nb += (double)b[i] * b[i];
  }
  if (na <= 0 || nb <= 0) return 0;
  return (float)(dot / (sqrt(na) * sqrt(nb)));
}

static void mapBoxToScreen(int imgW, int imgH, float x0, float y0, float x1, float y1, int &dx,
                           int &dy, int &dw, int &dh) {
  int side = imgW < imgH ? imgW : imgH;
  int cropX = (imgW - side) / 2;
  float s = 240.0f / (float)side;
  dx = (int)((x0 - cropX) * s);
  dy = (int)(y0 * s);
  dw = (int)((x1 - x0) * s);
  dh = (int)((y1 - y0) * s);
  if (dx < 0) {
    dw += dx;
    dx = 0;
  }
  if (dy < 0) {
    dh += dy;
    dy = 0;
  }
  if (dx + dw > 240) dw = 240 - dx;
  if (dy + dh > 240) dh = 240 - dy;
}

static void freeBoxes(box_array_t *boxes) {
  if (!boxes) return;
  free(boxes->score);
  free(boxes->box);
  free(boxes->landmark);
  free(boxes->category);
  free(boxes);
}

// 取一張影格、轉 RGB888、偵測人臉
// 回傳 boxes（呼叫端需 freeBoxes），並透過 fbOut 取得影格
static box_array_t *detectOnFrame(camera_fb_t **fbOut, dl_matrix3du_t &img) {
  *fbOut = nullptr;
  if (!s_camReady) return nullptr;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return nullptr;
  *fbOut = fb;

  if (!ensureRgbBuffer(fb->width, fb->height)) return nullptr;
  s_rgbFresh = false;
  if (!fmt2rgb888(fb->buf, fb->len, fb->format, s_rgb)) {
    logLine("CAM", "影格轉 RGB888 失敗");
    return nullptr;
  }
  s_rgbFresh = true;

  img.w = fb->width;
  img.h = fb->height;
  img.c = 3;
  img.n = 1;
  img.stride = fb->width * 3;
  img.item = (uc_t *)s_rgb;

  mtmn_config_t mtmn = mtmn_init_config();
  mtmn.min_face = 60;
  mtmn.pyramid = 0.707f;
  mtmn.pyramid_times = 4;
  mtmn.p_threshold.score = 0.6f;
  mtmn.p_threshold.nms = 0.7f;
  mtmn.p_threshold.candidate_number = 20;
  mtmn.r_threshold.score = 0.7f;
  mtmn.r_threshold.nms = 0.7f;
  mtmn.r_threshold.candidate_number = 10;
  mtmn.o_threshold.score = 0.7f;
  mtmn.o_threshold.nms = 0.4f;
  mtmn.o_threshold.candidate_number = 1;

  return face_detect(&img, &mtmn);
}

bool faceDetect(FaceResult &res, bool doRecognize, bool drawBox) {
  res = FaceResult();
  if (!s_camReady) return false;

  camera_fb_t *fb = nullptr;
  dl_matrix3du_t img;
  memset(&img, 0, sizeof(img));
  box_array_t *boxes = detectOnFrame(&fb, img);

  if (drawBox && fb) {
    displayFrame(fb, true); // detectOnFrame() 已把同一張影格解到 s_rgb
  }

  bool found = false;
  if (boxes && boxes->len > 0) {
    found = true;
    float x0 = boxes->box[0].box_p[0];
    float y0 = boxes->box[0].box_p[1];
    float x1 = boxes->box[0].box_p[2];
    float y1 = boxes->box[0].box_p[3];
    mapBoxToScreen(img.w, img.h, x0, y0, x1, y1, res.x, res.y, res.w, res.h);
    res.detected = true;

    if (!s_aligned) s_aligned = aligned_face_alloc();
    if (s_aligned && align_face(boxes, &img, s_aligned) == ESP_OK) {
      if (doRecognize && s_faceCount > 0) {
        dl_matrix3d_t *probe = get_face_id(s_aligned);
        if (probe) {
          int best = -1;
          float bestScore = 0;
          for (int i = 0; i < s_faceCount; i++) {
            float sc = cosineSimilarity(s_faces[i].vec, probe->item);
            if (sc > bestScore) {
              bestScore = sc;
              best = i;
            }
          }
          res.score = bestScore;
          if (best >= 0 && bestScore >= g_cfg.camera.rec_threshold) {
            res.recognized = true;
            res.name = String(s_faces[best].name);
          }
          dl_matrix3d_free(probe);
        }
      }
    }
  }

  freeBoxes(boxes);
  if (fb) esp_camera_fb_return(fb);

  if (drawBox) {
    if (res.detected) {
      uint16_t color = res.recognized ? COL_GREEN : COL_RED;
      g_disp.drawRect(res.x, res.y, res.w, res.h, color);
      g_disp.drawRect(res.x + 1, res.y + 1, res.w - 2, res.h - 2, color);
      if (res.w > 12 && res.h > 12) {
        // 四角加粗
        int len = res.w / 5;
        g_disp.drawHLine(res.x, res.y, len, color);
        g_disp.drawHLine(res.x + res.w - len, res.y, len, color);
        g_disp.drawHLine(res.x, res.y + res.h - 1, len, color);
        g_disp.drawHLine(res.x + res.w - len, res.y + res.h - 1, len, color);
      }
    }
  }
  return found;
}

bool faceEnroll(const String &rawName, EnrollProgressFn cb, String &err) {
  err = "";
  if (!s_camReady) {
    err = "相機未就緒";
    return false;
  }
  if (!ensureFaceArray()) {
    err = "記憶體不足";
    return false;
  }
  if (s_faceCount >= s_faceCap) {
    err = String("已達上限 ") + s_faceCap + " 個人臉";
    return false;
  }

  int need = g_cfg.camera.enroll_samples;
  if (need < 1) need = 1;

  float *acc = (float *)ps_malloc(FACE_VEC_SIZE * sizeof(float));
  if (!acc) acc = (float *)malloc(FACE_VEC_SIZE * sizeof(float));
  if (!acc) {
    err = "記憶體不足";
    return false;
  }
  memset(acc, 0, FACE_VEC_SIZE * sizeof(float));

  int got = 0;
  int attempts = 0;
  int maxAttempts = need * 8;
  uint32_t start = millis();

  while (got < need && attempts < maxAttempts && millis() - start < 30000) {
    attempts++;
    camera_fb_t *fb = nullptr;
    dl_matrix3du_t img;
    memset(&img, 0, sizeof(img));
    box_array_t *boxes = detectOnFrame(&fb, img);

    bool faceFound = false;
    int bx = 0, by = 0, bw = 0, bh = 0;
    if (boxes && boxes->len > 0) {
      mapBoxToScreen(img.w, img.h, boxes->box[0].box_p[0], boxes->box[0].box_p[1],
                     boxes->box[0].box_p[2], boxes->box[0].box_p[3], bx, by, bw, bh);
      if (!s_aligned) s_aligned = aligned_face_alloc();
      if (s_aligned && align_face(boxes, &img, s_aligned) == ESP_OK) {
        dl_matrix3d_t *id = get_face_id(s_aligned);
        if (id) {
          for (int i = 0; i < FACE_VEC_SIZE; i++) acc[i] += id->item[i];
          dl_matrix3d_free(id);
          got++;
          faceFound = true;
        }
      }
    }

    if (fb) displayFrame(fb, true);
    if (faceFound) {
      g_disp.drawRect(bx, by, bw, bh, COL_GREEN);
      g_disp.drawRect(bx + 1, by + 1, bw - 2, bh - 2, COL_GREEN);
    }
    if (cb) cb(got, need, faceFound, bx, by, bw, bh);

    freeBoxes(boxes);
    if (fb) esp_camera_fb_return(fb);
    delay(60);
  }

  if (got < need) {
    free(acc);
    err = String("只取得 ") + got + "/" + need + " 個樣本，請面向鏡頭再試";
    return false;
  }

  // 平均
  for (int i = 0; i < FACE_VEC_SIZE; i++) acc[i] /= (float)need;

  String name = sanitizeName(rawName);
  strncpy(s_faces[s_faceCount].name, name.c_str(), FACE_NAME_MAX - 1);
  s_faces[s_faceCount].name[FACE_NAME_MAX - 1] = 0;
  s_faces[s_faceCount].vec = acc;
  s_faceCount++;

  faceSave();
  logLine("FACE", "已新增人臉 ID：%s（共 %d 個）", name.c_str(), s_faceCount);
  return true;
}
