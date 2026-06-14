#pragma once
// audio_io — I2S 16 kHz mono capture (mic) + playback (amp) for the satellite.
// Uses the ESP-IDF/Arduino I2S driver. Pins come from audio_pins.h per variant.

#include <Arduino.h>

namespace hearth {

class AudioIO {
public:
    bool begin();
    // Read up to `max` int16 samples; returns count actually read (non-blocking-ish).
    size_t readMic(int16_t* out, size_t max);
    // Play a buffer of 16 kHz mono int16 to the amp (blocking until written).
    void   playPcm(const int16_t* samples, size_t n);
    static constexpr int kSampleRate = 16000;
};

} // namespace hearth
