#pragma once
//
// Capture — WASAPI microphone capture via miniaudio.
//
// Opens a capture device (default or named via audio.input_device), resamples
// to 16 kHz mono float, and feeds samples into a FloatRing the AudioService
// pump drains. The miniaudio callback only does the lock-free ring write.
//
// Mic-enabled gating: when disabled the device is fully stopped so the OS mic
// indicator turns off (privacy.mic_enabled contract).
//
#include "audio_common.h"
#include <memory>
#include <string>

namespace polymath::audio {

class Capture {
public:
    Capture();
    ~Capture();

    // Initialises (but does not start) the device. `device_name` empty → system
    // default; otherwise matches a capture device by name substring (case-
    // insensitive). Returns false if no device / init failed.
    bool init(const std::string& device_name = {});

    // Tear down and re-init with a (possibly new) device name. Stops capture
    // first. Used when settings change audio.input_device.
    bool reinit(const std::string& device_name);

    // Start/stop streaming into the ring. start() is idempotent.
    bool start();
    void stop();
    bool isRunning() const;

    // Currently selected device name (empty = default).
    const std::string& deviceName() const { return device_name_; }

    // The ring the worker drains. Stable for the lifetime of the Capture
    // object (reinit preserves the ring; only clears it).
    FloatRing& ring() { return ring_; }
    const FloatRing& ring() const { return ring_; }

public:
    struct Impl;   // opaque; defined in the .cpp (miniaudio callback needs it)
private:
    std::unique_ptr<Impl> d_;
    FloatRing             ring_{kRingCapacity};   // ~16 s @ 16 kHz
    std::string           device_name_;
};

} // namespace polymath::audio
