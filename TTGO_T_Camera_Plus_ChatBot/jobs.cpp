// ============================================================================
//  jobs.cpp - one background FreeRTOS task performs blocking AI / audio work
//             so that the single threaded WebServer keeps answering requests.
// ============================================================================
#include "jobs.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "app_config.h"
#include "audio.h"
#include "board_config.h"
#include "chat.h"
#include "storage.h"

struct JobRequest {
  uint8_t kind;
  String  text;
  int     seconds;
};

static QueueHandle_t     s_queue = nullptr;
static SemaphoreHandle_t s_mutex = nullptr;
static JobSnapshot       s_job;
static uint32_t          s_next_id = 1;
static uint32_t          s_started = 0;

static void run_request(JobRequest *req);
static void worker_task(void *arg);

static void job_lock()   { if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void job_unlock() { if (s_mutex) xSemaphoreGive(s_mutex); }

static const char *kind_name(uint8_t kind) {
  switch (kind) {
    case JOB_KIND_CHAT:  return "chat";
    case JOB_KIND_VOICE: return "voice";
    default:             return "none";
  }
}

static const char *status_name(uint8_t status) {
  switch (status) {
    case JOB_STATUS_RUNNING: return "running";
    case JOB_STATUS_DONE:    return "done";
    case JOB_STATUS_FAILED:  return "failed";
    default:                 return "idle";
  }
}

// ---------------------------------------------------------------------------
//  status publishing (worker task -> loop task)
// ---------------------------------------------------------------------------
static void job_progress(int percent, const char *phase) {
  job_lock();
  s_job.progress = percent;
  if (phase) s_job.phase = phase;
  job_unlock();
}

void jobs_begin() {
  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
  if (!s_queue) s_queue = xQueueCreate(3, sizeof(JobRequest *));

  s_job = JobSnapshot();

  xTaskCreatePinnedToCore(worker_task, "ai_worker", 24576, nullptr, 1, nullptr, 1);
}

static void worker_task(void *arg) {
  (void)arg;
  JobRequest *req = nullptr;
  for (;;) {
    if (xQueueReceive(s_queue, &req, portMAX_DELAY) == pdTRUE && req) {
      run_request(req);
      delete req;
      req = nullptr;
    }
  }
}

bool jobs_busy() {
  job_lock();
  bool busy = (s_job.status == JOB_STATUS_RUNNING);
  job_unlock();
  return busy;
}

void jobs_snapshot(JobSnapshot &out) {
  job_lock();
  out = s_job;
  job_unlock();
}

String jobs_status_json() {
  JobSnapshot js;
  jobs_snapshot(js);

  String out;
  out.reserve(256 + js.result.length() + js.error.length() + js.transcript.length());
  out  = "{\"ok\":true,\"id\":";
  out += (unsigned long)js.id;
  out += ",\"kind\":\"";
  out += kind_name(js.kind);
  out += "\",\"status\":\"";
  out += status_name(js.status);
  out += "\",\"busy\":";
  out += (js.status == JOB_STATUS_RUNNING) ? "true" : "false";
  out += ",\"progress\":";
  out += js.progress;
  out += ",\"phase\":\"";
  out += json_escape(js.phase);
  out += "\",\"result\":\"";
  out += json_escape(js.result);
  out += "\",\"transcript\":\"";
  out += json_escape(js.transcript);
  out += "\",\"error\":\"";
  out += json_escape(js.error);
  out += "\",\"elapsed_ms\":";
  out += (unsigned long)js.elapsed_ms;
  out += "}";
  return out;
}

// ---------------------------------------------------------------------------
//  starting jobs
// ---------------------------------------------------------------------------
static bool enqueue(JobRequest *req, String &err) {
  if (!s_queue) {
    err = "背景工作尚未啟動";
    delete req;
    return false;
  }
  if (jobs_busy()) {
    err = "上一個工作還在進行中，請稍候";
    delete req;
    return false;
  }

  job_lock();
  s_job.id         = s_next_id++;
  s_job.kind       = req->kind;
  s_job.status     = JOB_STATUS_RUNNING;
  s_job.progress   = 0;
  s_job.phase      = "排隊中";
  s_job.result     = "";
  s_job.transcript = "";
  s_job.error      = "";
  s_job.elapsed_ms = 0;
  s_started        = millis();
  job_unlock();

  if (xQueueSend(s_queue, &req, 0) != pdTRUE) {
    job_lock();
    s_job.status = JOB_STATUS_FAILED;
    s_job.error  = "工作佇列已滿";
    job_unlock();
    delete req;
    err = "工作佇列已滿";
    return false;
  }
  return true;
}

bool jobs_start_chat(const String &text, String &err) {
  if (text.length() == 0) {
    err = "訊息是空的";
    return false;
  }
  JobRequest *req = new JobRequest();
  req->kind = JOB_KIND_CHAT;
  req->text = text;
  req->seconds = 0;
  return enqueue(req, err);
}

bool jobs_start_voice(int seconds, String &err) {
  JobRequest *req = new JobRequest();
  req->kind = JOB_KIND_VOICE;
  req->seconds = seconds;
  return enqueue(req, err);
}

// ---------------------------------------------------------------------------
//  worker implementation
// ---------------------------------------------------------------------------
static void run_request(JobRequest *req) {
  String result, transcript, err;
  bool ok = false;

  if (req->kind == JOB_KIND_CHAT) {
    ok = chat_perform(req->text, result, err, job_progress);
  } else {
    AppConfig cfg;
    config_snapshot(cfg);

    int seconds = (req->seconds > 0) ? req->seconds : cfg.mic_seconds;
    if (seconds < 1)  seconds = 1;
    if (seconds > 20) seconds = 20;

    String derr;
    sd_ensure_dir(VOICE_DIR, derr);

    job_progress(2, "準備麥克風");
    ok = audio_record_wav(VOICE_WAV_FILE, seconds, cfg.mic_sample_rate, cfg.mic_gain,
                          [](int p, const char *phase) {
                            job_progress(2 + p * 52 / 100, phase);
                          },
                          err);

    if (ok && !cfg.stt_enabled) {
      ok  = false;
      err = "錄音已存到 SD 卡 /voice/voice.wav；若要直接對話，請在 AI 設定啟用語音辨識";
    }

    if (ok) {
      job_progress(56, "語音辨識");
      ok = chat_transcribe(VOICE_WAV_FILE, transcript, err,
                           [](int p, const char *phase) {
                             job_progress(56 + p * 18 / 100, phase);
                           });
    }

    if (ok && transcript.length()) {
      job_progress(78, "思考中");
      ok = chat_perform(transcript, result, err,
                        [](int p, const char *phase) {
                          job_progress(78 + p * 22 / 100, phase);
                        });
    }
  }

  job_lock();
  s_job.status     = ok ? JOB_STATUS_DONE : JOB_STATUS_FAILED;
  s_job.progress   = ok ? 100 : s_job.progress;
  s_job.phase      = ok ? "完成" : "失敗";
  s_job.result     = result;
  s_job.transcript = transcript;
  s_job.error      = ok ? "" : (err.length() ? err : "未知錯誤");
  s_job.elapsed_ms = millis() - s_started;
  job_unlock();
}
