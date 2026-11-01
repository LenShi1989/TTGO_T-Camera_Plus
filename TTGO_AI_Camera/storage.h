#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

// 顯示器與 SD 卡共用 HSPI 匯流排
extern SPIClass spiBus;

bool storageBegin();
bool storageReady();
void storageInfo(uint64_t &totalBytes, uint64_t &usedBytes);

String storageRead(const String &path);
bool storageWrite(const String &path, const String &data);
bool storageAppend(const String &path, const String &data);
bool storageExists(const String &path);
bool storageRemove(const String &path);
