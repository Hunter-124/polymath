#pragma once
//
// AsrWhisper — whisper.cpp transcription of a finished speech segment.
//
// Two configurations share this class via Mode:
//   * Command : higher-accuracy model (base.en), used after wake / PTT.
//               Loaded lazily on wake/PTT; unloaded after audio.asr_idle_unload_s
//               (default 90 s) of inactivity (04 §3.2).
//   * Ambient : tiny.en for continuous background transcription; loaded only
//               when privacy.ambient_transcription is enabled.
//
// transcribe() blocks the calling thread (whisper_full). AudioService posts
// jobs to a dedicated AsrWorker QThread so the pump never stalls (04 §3.3).
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
    // Safe to call when already loaded with the same path (no-op success).
    bool load(const std::filesystem::path& model_path, Mode mode);

    // Frees the whisper context and releases VRAM/RAM (04 §3.2 idle unload).
    void unload();

    bool ready() const { return ready_; }
    bool isLoaded() const { return ready_; }
    Mode mode() const { return mode_; }
    const std::filesystem::path& modelPath() const { return model_path_; }

    // Transcribes a mono 16 kHz float segment. Returns text (may be empty).
    // Writes mean confidence to *confidence when non-null.
    std::string transcribe(const std::vector<float>& pcm, float* confidence = nullptr);

    void setLanguage(const std::string& lang) { language_ = lang; }
    void setThreads(int n) { threads_ = n; }

    // Diagnostics / tests.
    int    loadCount() const { return load_count_; }
    int    unloadCount() const { return unload_count_; }
    void   resetStats() { load_count_ = unload_count_ = 0; }

private:
    struct Impl;
    std::unique_ptr<Impl> d_;

    bool        ready_    = false;
    Mode        mode_     = Mode::Command;
    std::string language_ = "en";
    int         threads_  = 4;
    std::filesystem::path model_path_;

    int load_count_   = 0;
    int unload_count_ = 0;
};

} // namespace polymath::audio
