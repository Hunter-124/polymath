#pragma once
//
// TtsPiper — Piper neural TTS synthesis + miniaudio playback.
//
// Synthesises text to 16-bit PCM with Piper (libpiper / piper.hpp, which wraps
// the ONNX voice + espeak-ng phonemiser), then plays it through a miniaudio
// playback device. Voices are selected per request by name; each voice is a
// (model.onnx, model.onnx.json) pair under <models>/piper/<voice>/.
//
// synthesize+play is synchronous and runs on the AudioService worker thread.
// While speaking, capture should be paused by the caller to avoid the assistant
// transcribing its own voice (barge-in handling lives in AudioService).
//
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace polymath::audio {

class TtsPiper {
public:
    TtsPiper();
    ~TtsPiper();

    // Sets the directory holding per-voice subfolders and the default voice
    // name to fall back to when a request specifies none/unknown.
    bool init(const std::filesystem::path& voices_dir, const std::string& default_voice);
    bool ready() const { return ready_; }

    // Synthesises `text` with `voice` (empty -> default) to int16 mono PCM and
    // plays it. Returns false on synth/playback failure. Blocks until done.
    bool speak(const std::string& text, const std::string& voice);

    // Synthesis only (no playback) — exposed for tests / saving to file.
    bool synthesize(const std::string& text, const std::string& voice,
                    std::vector<int16_t>& out_pcm, int& out_sample_rate);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    bool ready_ = false;
};

} // namespace polymath::audio
