// ============================================================================
//  jobs.h - background AI worker (chat + voice) with pollable status
// ============================================================================
#ifndef TTGO_T_CAMERA_PLUS_JOBS_H
#define TTGO_T_CAMERA_PLUS_JOBS_H

#include <Arduino.h>

enum JobKind : uint8_t {
  JOB_KIND_NONE  = 0,
  JOB_KIND_CHAT  = 1,
  JOB_KIND_VOICE = 2
};

enum JobStatus : uint8_t {
  JOB_STATUS_IDLE    = 0,
  JOB_STATUS_RUNNING = 1,
  JOB_STATUS_DONE    = 2,
  JOB_STATUS_FAILED  = 3
};

struct JobSnapshot {
  uint32_t id         = 0;
  uint8_t  kind       = JOB_KIND_NONE;
  uint8_t  status     = JOB_STATUS_IDLE;
  int      progress   = 0;
  String   phase;
  String   result;
  String   transcript;
  String   error;
  uint32_t elapsed_ms = 0;
};

void   jobs_begin();
bool   jobs_start_chat(const String &text, String &err);
bool   jobs_start_voice(int seconds, String &err);
bool   jobs_busy();
void   jobs_snapshot(JobSnapshot &out);
String jobs_status_json();

#endif  // TTGO_T_CAMERA_PLUS_JOBS_H
