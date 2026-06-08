#pragma once
//
// WakeWord — openWakeWord detector running on ONNX Runtime.
//
// openWakeWord is a 3-stage chain shared across all wake phrases:
//   1. melspectrogram.onnx : raw 16 kHz audio -> log-mel features (32 bins)
//   2. embedding_model.onnx : 76x32 mel window -> 96-dim speech embedding
//   3. <wakeword>.onnx       : sequence of 16 embeddings -> P(wake)  in [0,1]
//
// Stages 1 & 2 are reused for every phrase; only stage 3 swaps per phrase.
// We feed audio incrementally (80 ms / 1280-sample frames) and emit when the
// classifier score crosses the threshold (with a small refractory period so a
// single utterance fires once).
//
// Model files are expected under <models>/wakeword/:
//   melspectrogram.onnx, embedding_model.onnx, <phrase>.onnx
// The phrase comes from Config keys::WakeWord (default "hey_jarvis").
//
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace polymath::audio {

class WakeWord {
public:
    WakeWord();
    ~WakeWord();

    // Loads the shared mel+embedding models and the per-phrase classifier from
    // `model_dir`. `phrase` is the model basename (e.g. "hey_jarvis"). Returns
    // false if any model is missing/failed to load (detection then disabled).
    bool load(const std::filesystem::path& model_dir, const std::string& phrase);
    bool ready() const { return ready_; }
    const std::string& phrase() const { return phrase_; }

    // Detection threshold in [0,1]; default 0.5 (openWakeWord recommendation).
    void setThreshold(float t) { threshold_ = t; }

    // Feed one 1280-sample (80 ms) frame of float PCM. Returns true on the frame
    // that crosses the threshold (after the refractory period). Returns the
    // score via *score when non-null.
    bool process(const float* frame, int n, float* score = nullptr);

    // Drop accumulated state (call when capture restarts).
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;

    std::string  phrase_;
    bool         ready_     = false;
    float        threshold_ = 0.5f;

    // Refractory: after a hit, suppress further hits for N frames (~1 s).
    int refractory_left_ = 0;
};

} // namespace polymath::audio
