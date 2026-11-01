#pragma once
#include <Arduino.h>

struct AiReply {
  bool ok = false;
  int status = 0;
  String user;  // 語音辨識出的文字（文字聊天時為空）
  String reply; // AI 回覆（繁體中文）
  String error;
};

// 純文字聊天
bool geminiChatText(const String &text, AiReply &out);

// 語音聊天：wav 為完整 WAV 檔（16kHz / 16-bit / 單聲道）
bool geminiChatAudio(const uint8_t *wav, size_t wavLen, AiReply &out);

bool geminiReady();
