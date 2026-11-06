// ============================================================================
//  chat.h - AI provider calls (OpenAI compatible + Ollama) and Whisper STT
// ============================================================================
#ifndef TTGO_T_CAMERA_PLUS_CHAT_H
#define TTGO_T_CAMERA_PLUS_CHAT_H

#include <Arduino.h>

typedef void (*ChatProgressFn)(int percent, const char *phase);

void   chat_begin();
void   chat_reset();
String chat_history_json();
int    chat_history_count();

// Blocking - must be called from the worker task, never from loop().
bool chat_perform(const String &user_text, String &reply, String &err,
                  ChatProgressFn progress);
bool chat_transcribe(const String &wav_path, String &text, String &err,
                     ChatProgressFn progress);

#endif  // TTGO_T_CAMERA_PLUS_CHAT_H

// 一體化語音對話：錄音 + STT + AI 回應一步完成
bool chat_voice_chat(const String &wav_path, String &transcript, String &reply, String &err,
                     ChatProgressFn progress);
