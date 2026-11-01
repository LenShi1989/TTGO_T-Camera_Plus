#pragma once
#include <Arduino.h>

// 錄音進度回呼：level 0..100，elapsedMs 為已錄音時間
typedef void (*AudioProgressFn)(int level, uint32_t elapsedMs);

bool audioBegin();
void audioEnd();
bool audioReady();

// 錄音到 dst（最多 maxSamples 個 int16）。
// 達到 maxMs、或超過 minMs 後連續靜音 silenceMs，即停止。
// 回傳實際樣本數。
size_t audioRecord(int16_t *dst, size_t maxSamples, uint32_t maxMs, uint32_t minMs,
                   uint32_t silenceMs, int vadThreshold, int gain, AudioProgressFn progress);

// 簡易音量測試（給系統狀態頁用）
int audioLevelOnce();
