#include "wakeword.h"

namespace hearth {

bool WakeWord::begin() {
#ifdef HEARTH_HAVE_MWW
    // TODO(model): init microWakeWord (TFLite-Micro). Allocate the interpreter,
    // load the streaming model + the 40-channel log-mel frontend, set
    // modelReady_ = true. feed() then runs the model per 20ms window and fires on
    // the trained phrase.
    modelReady_ = false;   // until the .tflite is wired in
#else
    modelReady_ = false;
#endif
    return true;
}

bool WakeWord::feed(const int16_t* samples, size_t n) {
    if (modelReady_) {
        // TODO(model): run microWakeWord on this window; return its detection.
        return false;
    }
    // Fallback energy gate: track an EMA of frame energy; "wake" when a loud
    // burst rises well above the noise floor, then stay open ~2s so a command can
    // follow. Documented stand-in for a real wake model.
    double sum = 0;
    for (size_t i = 0; i < n; ++i) { double s = samples[i]; sum += s * s; }
    float rms = (n ? sqrtf((float)(sum / n)) : 0);
    ema_ = ema_ * 0.95f + rms * 0.05f;
    uint32_t now = millis();
    if (rms > ema_ * 4.0f && rms > 800.0f && now > openUntil_) {
        openUntil_ = now + 2000;
        return true;
    }
    return false;
}

const char* WakeWord::name() const {
    return modelReady_ ? "microWakeWord" : "energy-gate-stub";
}

} // namespace hearth
