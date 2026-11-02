#pragma once
#include <Arduino.h>

void webuiBegin();
void webuiHandle();
bool webuiActive();

// SPIFFS（前端 HTML/CSS/JS）是否掛載成功
bool webuiSpiffsReady();
