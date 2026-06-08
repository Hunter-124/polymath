#pragma once
//
// Capture — WASAPI microphone capture via miniaudio.
//
// Opens the default capture device, resamples/converts to the canonical
// 16 kHz mono float pipeline format, and feeds samples into a FloatRing the
// AudioService worker drains.  The miniaudio data callback runs on a realtime
// audio thread, so it only does the lock-free ring write — no allocation, no
// logging, no blocking.
//
// Mic-enabled gating: when disabled, the device is fully stopped (released) so
// the OS mic indicator turns off, honouring privacy.mic_enabled.
//
#include "audio_common.h"
#include <memory>

namespace polymath::audio {

class Capture {
public:
    Capture();
    ~Capture();

    // Initialises (but does not start) the device. Returns false if no capture
    // device / device init failed. Safe to call once.
    bool init();

    // Start/stop streaming into the ring. start() is idempotent; calling it when
    // mic is disabled is a no-op. stop() releases the device (mic indicator off).
    bool start();
    void stop();
    bool isRunning() const;

    // The ring the worker drains. Stable for the lifetime of the Capture.
    FloatRing& ring() { return ring_; }

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    FloatRing             ring_{1u << 16};   // ~4 s @ 16 kHz
};

} // namespace polymath::audio
