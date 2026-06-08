#pragma once
//
// Vad — Silero VAD (ONNX) streaming speech gate.
//
// Silero v4/v5 takes a fixed 512-sample window @ 16 kHz plus a recurrent state
// and returns P(speech) in [0,1]. We wrap it in a small state machine that emits
// speech segment boundaries with configurable padding and minimum silence so a
// brief pause inside a sentence does not split the utterance.
//
// Model file: <models>/vad/silero_vad.onnx
//
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace polymath::audio {

class Vad {
public:
    enum class Event { None, SpeechStart, SpeechEnd };

    Vad();
    ~Vad();

    bool load(const std::filesystem::path& model_path);
    bool ready() const { return ready_; }

    // Tuning (seconds / probability). Defaults follow Silero's recommendations.
    void setThreshold(float p)        { threshold_ = p; }
    void setMinSilenceMs(int ms)      { min_silence_ms_ = ms; }
    void setSpeechPadMs(int ms)       { speech_pad_ms_ = ms; }

    // Feed one 512-sample window. Returns a boundary event (or None) and the raw
    // speech probability via *prob when non-null.
    Event process(const float* window512, float* prob = nullptr);

    bool inSpeech() const { return in_speech_; }
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;

    bool  ready_           = false;
    float threshold_       = 0.5f;
    int   min_silence_ms_  = 300;
    int   speech_pad_ms_   = 100;

    bool  in_speech_       = false;
    int   silence_ms_      = 0;
};

} // namespace polymath::audio
