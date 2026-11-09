// ============================================================================
//  board_config.h - hardware map and global limits for the
//  TTGO T-Camera Plus AI ChatBot firmware.
//
//  Sources:
//    esp32-camera-screen-master/main/include/board_def.h
//    README.md (TTGO Camera Plus pinout)
// ============================================================================
#ifndef TTGO_T_CAMERA_PLUS_BOARD_CONFIG_H
#define TTGO_T_CAMERA_PLUS_BOARD_CONFIG_H

// ---------------------------------------------------------------------------
//  SPI bus - shared by the ST7789 panel and the SD card
// ---------------------------------------------------------------------------
#define BOARD_SPI_MISO 22
#define BOARD_SPI_MOSI 19
#define BOARD_SPI_SCLK 21

// ---------------------------------------------------------------------------
//  microSD card (CS on GPIO0 - a strapping pin, driven HIGH early in setup)
// ---------------------------------------------------------------------------
#define BOARD_SD_CS        0
#define SD_MOUNT_POINT     "/sd"
#define SD_SPI_FREQUENCY   20000000   // 20 MHz is safe on this shared bus

// ---------------------------------------------------------------------------
//  I2S microphone (ICS-43434 / INMP441 style, 32 bit stereo frames)
// ---------------------------------------------------------------------------
#define BOARD_MIC_BCLK 14
#define BOARD_MIC_WS   32
#define BOARD_MIC_DATA 33

// ---------------------------------------------------------------------------
//  I2C bus (not used by this firmware, kept for reference / future sensors)
//  The OV2640 SCCB lines also live on these two pins.
// ---------------------------------------------------------------------------
#define BOARD_I2C_SDA 18
#define BOARD_I2C_SCL 23

// ---------------------------------------------------------------------------
//  OV2640 camera (parallel DVP bus + SCCB on the I2C pins above)
//  Pin numbers match esp32-camera-screen-master/main/include/board_def.h.
// ---------------------------------------------------------------------------
#define BOARD_CAM_PWDN    -1
#define BOARD_CAM_RESET   -1
#define BOARD_CAM_XCLK    4
#define BOARD_CAM_SIOD    18    // SCCB SDA
#define BOARD_CAM_SIOC    23    // SCCB SCL

#define BOARD_CAM_D0      34    // Y2
#define BOARD_CAM_D1      13    // Y3
#define BOARD_CAM_D2      26    // Y4
#define BOARD_CAM_D3      35    // Y5
#define BOARD_CAM_D4      39    // Y6
#define BOARD_CAM_D5      38    // Y7
#define BOARD_CAM_D6      37    // Y8
#define BOARD_CAM_D7      36    // Y9

#define BOARD_CAM_VSYNC   5
#define BOARD_CAM_HREF    27
#define BOARD_CAM_PCLK    25
#define BOARD_CAM_XCLK_HZ 20000000

// ---------------------------------------------------------------------------
//  Application constants
// ---------------------------------------------------------------------------
#define APP_NAME        "T-Camera Plus AI"
#define APP_VERSION     "1.7.0"

#define CONFIG_FILE_SD      "/config.json"   // relative to /sd
#define CONFIG_FILE_SPIFFS  "/config.json"   // relative to SPIFFS
#define FONT_FILE           "/font_tc16.bin"
#define VOICE_DIR           "/voice"
#define VOICE_WAV_FILE      "/voice/voice.wav"
#define PHOTO_DIR           "/photos"
#define PHOTO_NAME_PREFIX   "photo_"
#define PHOTO_PREVIEW_SECONDS   3            // live TFT viewfinder before the shutter

#define MAX_PATH_LEN            200
#define SD_LIST_MAX_ENTRIES     400
#define SD_TEXT_MAX_BYTES       32768        // 32 KiB text editor limit
#define CONFIG_MAX_BYTES        16384
#define UPLOAD_MAX_BYTES        (64ULL * 1024ULL * 1024ULL)

#define TFT_WIDTH_PX   240
#define TFT_HEIGHT_PX  240

#define STATUS_BAR_H     20
#define FACE_AREA_Y      20
#define FACE_AREA_H      108
#define TEXT_AREA_Y      130
#define TEXT_AREA_H      (TFT_HEIGHT_PX - TEXT_AREA_Y)

#endif  // TTGO_T_CAMERA_PLUS_BOARD_CONFIG_H
