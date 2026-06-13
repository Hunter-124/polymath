#pragma once
// wakeword — on-device wake gate (FABRIC.md §8). Real build uses microWakeWord
// (a TFLite-Micro streaming model). If the model/lib isn't bundled
// (HEARTH_HAVE_MWW undefined) this compiles as an energy-threshold "push gate"
// stub so the satellite still builds and streams (it just opens on loud speech
// instead of a true wake phrase). Swap in microWakeWord by dropping the model and
// defining HEARTH_HAVE_MWW.

#include <Arduino.h>

namespace hearth {

class WakeWord {
public:
    bool begin();
    // Feed a block of 16 kHz mono int16 samples. Returns true on a wake event.
    bool feed(const int16_t* samples, size_t n);
    const char* name() const;
private:
    bool   modelReady_ = false;
    // energy-gate fallback state
    float  ema_ = 0;
    uint32_t openUntil_ = 0;
};

} // namespace hearth
