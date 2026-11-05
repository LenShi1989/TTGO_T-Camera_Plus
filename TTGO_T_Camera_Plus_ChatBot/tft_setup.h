// ============================================================================
//  tft_setup.h - TFT_eSPI configuration embedded in the sketch folder.
//
//  TFT_eSPI automatically picks this file up (see TFT_eSPI.h) so the library
//  User_Setup.h does NOT need to be edited.  Hardware: TTGO T-Camera Plus,
//  ST7789 240x240 IPS panel sharing the SPI bus with the SD card.
// ============================================================================
#ifndef TTGO_T_CAMERA_PLUS_TFT_SETUP_H
#define TTGO_T_CAMERA_PLUS_TFT_SETUP_H

#define USER_SETUP_LOADED

#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// Shared SPI bus (same pins as the SD card)
#define TFT_MISO 22
#define TFT_MOSI 19
#define TFT_SCLK 21

#define TFT_CS   12
#define TFT_DC   15
#define TFT_RST  -1     // not wired on this board
#define TFT_BL    2
#define TFT_BACKLIGHT_ON HIGH

// The panel is a 240x240 IPS unit; colours are correct without BGR swap.
// #define TFT_RGB_ORDER TFT_RGB

#define LOAD_GLCD       // 5x7 built-in font, used for the tiny status bar
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  16000000

#endif  // TTGO_T_CAMERA_PLUS_TFT_SETUP_H
