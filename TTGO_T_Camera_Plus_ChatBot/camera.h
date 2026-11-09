// ============================================================================
//  camera.h - OV2640 capture for the voice "拍照" command
// ============================================================================
#ifndef TTGO_T_CAMERA_PLUS_CAMERA_H
#define TTGO_T_CAMERA_PLUS_CAMERA_H

#include <Arduino.h>

// Initialises the OV2640 in JPEG mode.  Safe to call more than once; a failed
// init leaves camera_ready() == false so the rest of the firmware keeps
// working (chat / STT do not depend on the camera).
bool camera_begin(String &err);
bool camera_ready();

// True when the recognised speech looks like a request to take a photo.
bool camera_is_photo_command(const String &text);

// Shows a live viewfinder on the TFT for `seconds`, then restores the JPEG
// capture mode.  The camera is briefly re-initialised in RGB565 mode, so call
// this from the worker task and not from loop().  Returns false if the
// preview could not be shown (the caller may still try to capture).
bool camera_preview(uint8_t seconds);

// Grabs one JPEG frame and writes it to /photos/photo_XXXX.jpg.
// `path` receives the SD path (relative to the card root) and `bytes` the
// JPEG size.  Call from the worker task, not from loop().
bool camera_capture_to_sd(String &path, size_t &bytes, String &err);

#endif  // TTGO_T_CAMERA_PLUS_CAMERA_H