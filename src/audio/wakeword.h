#pragma once
//
// WakeWord — openWakeWord detector running on ONNX Runtime.
//
// 3-stage chain:
//   1. melspectrogram.onnx : raw 16 kHz audio -> log-mel features (32 bins)
//   2. embedding_model.onnx : 76x32 mel window -> 96-dim speech embedding
//   3. <wakeword>.onnx       : sequence of 16 embeddings -> P(wake) in [0,1]
//
// AudioService only feeds frames during VAD speech (+ 640 ms hangover) so idle
// CPU stays near zero (04 §3.1). ONNX failures use reload backoff (04 §3.6).
//
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace polymath::audio {

class WakeWord {
public:
    using NoticeFn = std::function<void(const char* level, const char* source,
                                        const char* message)>;

    WakeWord();
    ~WakeWord();

    // Loads mel+embedding+classifier from `model_dir`. `phrase` is the model
    // basename (e.g. "hey_jarvis"). Returns false if any model is missing.
    bool load(const std::filesystem::path& model_dir, const std::string& phrase);
    bool ready() const { return ready_; }
    const std::string& phrase() const { return phrase_; }

    void setNoticeFn(NoticeFn fn) { notice_fn_ = std::move(fn); }

    // Detection threshold in [0,1]; default 0.5. Barge-in raises by +0.1.
    void setThreshold(float t) { threshold_ = t; }
    float threshold() const    { return threshold_; }

    // Feed float PCM (typically one 1280-sample / 80 ms frame). Returns true on
    // the frame that crosses the threshold (after the refractory period).
    bool process(const float* frame, int n, float* score = nullptr);

    // Drop accumulated state (call when capture restarts / mode transitions).
    void reset();

    // Diagnostics / tests: how many times process() ran the ONNX chain.
    size_t processCalls() const { return process_calls_; }
    void   resetProcessCalls()  { process_calls_ = 0; }

public:
    struct Impl;   // opaque; defined in the .cpp
private:
    bool tryReload();
    void onInferenceError(const char* what);

    std::unique_ptr<Impl> d_;

    std::filesystem::path model_dir_;
    std::string  phrase_;
    NoticeFn     notice_fn_;
    bool         ready_     = false;
    float        threshold_ = 0.5f;

    int refractory_left_ = 0;

    int     reload_attempts_ = 0;
    int64_t next_retry_ms_   = 0;

    size_t process_calls_ = 0;
};

} // namespace polymath::audio
