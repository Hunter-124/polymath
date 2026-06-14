#pragma once
// audio_pins.h — I2S pin maps per hardware variant. Exactly one HEARTH_HW_* is
// defined by platformio.ini build_flags.

#if defined(HEARTH_HW_RESPEAKER)
  // Seeed ReSpeaker Lite (XIAO ESP32-S3 carrier). Mic + amp share the codec.
  #define MIC_I2S_BCLK   8
  #define MIC_I2S_LRCK   7
  #define MIC_I2S_DIN    44
  #define SPK_I2S_BCLK   8
  #define SPK_I2S_LRCK   7
  #define SPK_I2S_DOUT   43
  #define HEARTH_HW_NAME "respeaker-lite"

#elif defined(HEARTH_HW_XVF3800)
  // XMOS XVF3800 dev kit: far-field array + AEC; presents I2S to the S3.
  #define MIC_I2S_BCLK   5
  #define MIC_I2S_LRCK   6
  #define MIC_I2S_DIN    4
  #define SPK_I2S_BCLK   5
  #define SPK_I2S_LRCK   6
  #define SPK_I2S_DOUT   3
  #define HEARTH_HW_NAME "xvf3800"

#else  // HEARTH_HW_BUNDLED_MIC (default)
  // ICS-43434 MEMS mic (input) + MAX98357A amp (output) — separate I2S buses.
  #define MIC_I2S_BCLK   41
  #define MIC_I2S_LRCK   42
  #define MIC_I2S_DIN    2
  #define SPK_I2S_BCLK   15
  #define SPK_I2S_LRCK   16
  #define SPK_I2S_DOUT   7
  #define HEARTH_HW_NAME "bundled-mic"
#endif
