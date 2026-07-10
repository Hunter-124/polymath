#pragma once
//
// TtsPiper — persistent Piper neural TTS + streaming miniaudio playback.
//
// 04 §3.4:
//   * Spawn piper.exe once (--output_raw, stdin line-per-utterance); reuse
//     across utterances. Watchdog restarts on crash/exit.
//   * Sentence chunking: split on sentence boundaries; synth+queue so first
//     audio lands after the first sentence, not the full reply.
//   * Persistent non-blocking miniaudio playback device fed from a queue
//     (replaces per-call temp device + 10 ms poll loop).
//
// 04 §3.5 barge-in: stop() flushes the playback queue immediately.
//
// synthesize+play may block the calling worker thread; AudioService posts
// speak() onto a dedicated TTS QThread so the capture pump stays live.
//
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace polymath::audio {

class TtsPiper {
public:
    TtsPiper();
    ~TtsPiper();

    // Sets the directory holding per-voice subfolders and the default voice.
    // `output_device` empty → system default playback device.
    bool init(const std::filesystem::path& voices_dir,
              const std::string& default_voice,
              const std::string& output_device = {});
    bool ready() const { return ready_; }

    // Update playback device name (takes effect on next device ensure).
    void setOutputDevice(const std::string& name);

    // Synthesises `text` with `voice` (empty -> default), sentence-chunks it,
    // and plays via the persistent queue. Blocks until playback of all chunks
    // finishes or stop() is called. Returns false on hard failure.
    bool speak(const std::string& text, const std::string& voice);

    // Cancel current / pending playback (barge-in). Thread-safe.
    void stop();

    // True while the playback queue still has audio (or synth is in flight).
    bool isSpeaking() const;

    // Synthesis only (no playback) — full text as one shot. For tests / fixtures.
    bool synthesize(const std::string& text, const std::string& voice,
                    std::vector<int16_t>& out_pcm, int& out_sample_rate);

    // Split text on sentence boundaries. Public for unit tests (chunk ordering).
    static std::vector<std::string> splitSentences(const std::string& text);

    // Synthesize each sentence in order; append PCM per chunk into `chunks`.
    // Used by tests to verify chunk ordering without playback.
    bool synthesizeSentences(const std::string& text, const std::string& voice,
                             std::vector<std::vector<int16_t>>& chunks,
                             int& out_sample_rate);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    bool ready_ = false;
};

} // namespace polymath::audio
