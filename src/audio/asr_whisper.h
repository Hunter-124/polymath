#pragma once
//
// AsrWhisper — whisper.cpp transcription of a finished speech segment.
//
// Two configurations share this class via Mode:
//   * Command : higher-accuracy model (e.g. base.en / small.en), used after a
//               wake word or push-to-talk. Greedy+beam, more threads.
//   * Ambient : tiny model (tiny.en) for continuous background transcription;
//               cheaper, lower priority, results flagged is_ambient.
//
// transcribe() blocks the calling worker thread (whisper_full is synchronous)
// and returns the recognised text + a mean-logprob-derived confidence. The
// AudioService is responsible for publishing the Utterance on the EventBus.
//
// Model files: <models>/whisper/ggml-<name>.bin
//
#include "types.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace polymath::audio {

class AsrWhisper {
public:
    enum class Mode { Command, Ambient };

    AsrWhisper();
    ~AsrWhisper();

    // Loads a ggml whisper model. `mode` only affects decoding defaults.
    bool load(const std::filesystem::path& model_path, Mode mode);
    bool ready() const { return ready_; }
    Mode mode() const { return mode_; }

    // Transcribes a mono 16 kHz float segment. Returns text (may be empty for
    // silence/non-speech). Writes mean confidence to *confidence when non-null.
    std::string transcribe(const std::vector<float>& pcm, float* confidence = nullptr);

    // Optional language hint ("en", "auto"). Default "en".
    void setLanguage(const std::string& lang) { language_ = lang; }
    void setThreads(int n) { threads_ = n; }

private:
    struct Impl;
    std::unique_ptr<Impl> d_;

    bool        ready_    = false;
    Mode        mode_     = Mode::Command;
    std::string language_ = "en";
    int         threads_  = 4;
};

} // namespace polymath::audio
