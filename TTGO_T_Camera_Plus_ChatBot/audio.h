// ============================================================================
//  audio.h - I2S microphone recording to a 16 bit mono WAV file
// ============================================================================
#ifndef TTGO_T_CAMERA_PLUS_AUDIO_H
#define TTGO_T_CAMERA_PLUS_AUDIO_H

#include <Arduino.h>

typedef void (*AudioProgressFn)(int percent, const char *phase);

// Records `seconds` of audio from the on board I2S microphone and stores it
// as a mono 16 bit PCM WAV file.  path is relative to the SD root ("/voice/x.wav").
bool audio_record_wav(const String &path, int seconds, int sample_rate,
                      int gain_percent, AudioProgressFn progress, String &err);

// Latest input level (0..100), useful for the on screen VU meter.
int audio_level();

#endif  // TTGO_T_CAMERA_PLUS_AUDIO_H
