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

    // Engine preference consulted by init(): "auto" (file-presence autodetect,
    // default — Kokoro if its worker+model are present, else Piper), "kokoro"
    // (force; falls back to Piper with a warning if Kokoro files are missing),
    // "piper" (force classic Piper even when Kokoro is present). Call before
    // init(); a later call only takes effect on the next init().
    void setEnginePreference(const std::string& pref);

    // Updates the global fallback voice used when a caller passes an empty
    // voice (e.g. no per-persona voice set). Takes effect immediately (no
    // process restart) — the next speak()/warmUp() call picks it up. Accepts
    // bare Kokoro ids (af_heart, am_adam, ...) or legacy Piper ids.
    void setDefaultVoice(const std::string& voice);

    // Speed multiplier forwarded to the Kokoro worker inline (`!speed=`).
    // Applies live to an already-running process; new processes are spawned
    // with it too. Clamped to a safe range. No-op for the Piper engine (no
    // speed control in that CLI).
    void setSpeed(double speed);

    // Output gain applied to PCM right before it is queued for playback
    // (1.0 = unchanged). Takes effect on the next enqueued chunk — does not
    // affect synthesize()/synthesizeSentences() (offline paths used by
    // tests/fixtures, which return raw PCM for inspection).
    void setVolume(double volume);

    // Synthesises `text` with `voice` (empty -> default), sentence-chunks it,
    // and plays via the persistent queue. Blocks until playback of all chunks
    // finishes or stop() is called. Returns false on hard failure.
    // When `append` is true, does not clear the playback queue (streaming TTS)
    // and does not wait for drain — call endStream() when the reply is done.
    bool speak(const std::string& text, const std::string& voice, bool append = false);

    // Wait until the playback queue is empty (end of a streamed reply).
    void endStream();

    // Pre-spawn piper + warm the voice so the first real sentence is not cold.
    bool warmUp(const std::string& voice = {});

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
