#pragma once
//
// Vad — Silero VAD (ONNX) streaming speech gate.
//
// Silero v4/v5 takes a fixed 512-sample window @ 16 kHz plus a recurrent state
// and returns P(speech) in [0,1]. Wrapped in a state machine that emits speech
// segment boundaries with configurable padding and minimum silence.
//
// Always-on first gate in the idle chain (04 §3.1): oWW only runs while this
// reports speech (+ hangover managed by AudioService).
//
// ONNX inference failures trigger exponential-backoff session reload
// (1/5/30 s × 3) instead of a permanent ready_=false (04 §3.6).
//
// Model file: <models>/vad/silero_vad.onnx
//
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace polymath::audio {

class Vad {
public:
    enum class Event { None, SpeechStart, SpeechEnd };

    // Optional notice publisher (level, source, message) used on final reload fail.
    using NoticeFn = std::function<void(const char* level, const char* source,
                                        const char* message)>;

    Vad();
    ~Vad();

    bool load(const std::filesystem::path& model_path);
    bool ready() const { return ready_; }

    void setNoticeFn(NoticeFn fn) { notice_fn_ = std::move(fn); }

    // Tuning (seconds / probability). Defaults follow Silero's recommendations.
    void setThreshold(float p)        { threshold_ = p; }
    float threshold() const           { return threshold_; }
    void setMinSilenceMs(int ms)      { min_silence_ms_ = ms; }
    void setSpeechPadMs(int ms)       { speech_pad_ms_ = ms; }

    // Feed one 512-sample window. Returns a boundary event (or None) and the raw
    // speech probability via *prob when non-null.
    Event process(const float* window512, float* prob = nullptr);

    bool inSpeech() const { return in_speech_; }
    void reset();

    // Test / diagnostics: successful inference count since load/resetStats.
    size_t inferenceCount() const { return inference_count_; }
    void   resetStats()           { inference_count_ = 0; }

private:
    bool tryReload();          // rebuild session from stored path
    void onInferenceError(const char* what);

    struct Impl;
    std::unique_ptr<Impl> d_;

    std::filesystem::path model_path_;
    NoticeFn notice_fn_;

    bool  ready_           = false;
    float threshold_       = 0.5f;
    int   min_silence_ms_  = 300;
    int   speech_pad_ms_   = 100;

    bool  in_speech_       = false;
    int   silence_ms_      = 0;

    // Reload backoff state (04 §3.6).
    int   reload_attempts_ = 0;
    int64_t next_retry_ms_ = 0;   // steady_clock ms since epoch; 0 = none scheduled

    size_t inference_count_ = 0;
};

} // namespace polymath::audio
