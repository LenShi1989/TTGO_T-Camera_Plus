#pragma once
// TTGO T-Camera Plus 硬體接腳定義
// 參考 esp32-camera-screen-master/main/include/board_def.h

// ---------- 共用 SPI 匯流排 (HSPI) ----------
#define PIN_SPI_MISO 22
#define PIN_SPI_MOSI 19
#define PIN_SPI_SCLK 21

// ---------- ST7789 TFT (240x240) ----------
#define PIN_TFT_CS 12
#define PIN_TFT_DC 15
#define PIN_TFT_BL 2
#define PIN_TFT_RST -1 // 未接

#define TFT_WIDTH 240
#define TFT_HEIGHT 240

// ---------- microSD ----------
#define PIN_SD_CS 0

// ---------- I2C (IP5306 電源管理) ----------
#define PIN_I2C_SDA 18
#define PIN_I2C_SCL 23
#define I2C_BUS_NUM 1 // Wire1，相機 SCCB 使用 I2C0

// ---------- OV2640 相機 ----------
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 4
#define CAM_PIN_SIOD 18
#define CAM_PIN_SIOC 23
#define CAM_PIN_D7 36
#define CAM_PIN_D6 37
#define CAM_PIN_D5 38
#define CAM_PIN_D4 39
#define CAM_PIN_D3 35
#define CAM_PIN_D2 26
#define CAM_PIN_D1 13
#define CAM_PIN_D0 34
#define CAM_PIN_VSYNC 5
#define CAM_PIN_HREF 27
#define CAM_PIN_PCLK 25
#define CAM_XCLK_HZ 20000000

// ---------- I2S 麥克風 ----------
#define PIN_MIC_BCK 14
#define PIN_MIC_WS 32
#define PIN_MIC_DATA 33
#define I2S_MIC_PORT 1 // 相機使用 I2S0

// ---------- 電源管理 ----------
#define IP5306_ADDR 0x75

// ---------- LEDC 通道 ----------
#define LEDC_CH_TFT_BL 1 // 相機 XCLK 使用 channel 0
#define LEDC_TIMER_TFT_BL 1
