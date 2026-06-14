#pragma once
//
// TtsPiper — Piper neural TTS synthesis + miniaudio playback.
//
// Synthesises text to 16-bit PCM with Piper (libpiper / piper.hpp, which wraps
// the ONNX voice + espeak-ng phonemiser), then plays it through a miniaudio
// playback device. Voices are selected per request by name; each voice is a
// (model.onnx, model.onnx.json) pair under <models>/piper/<voice>/.
//
// Two playback paths:
//   * speak()       — synthesize the whole text, then play it; BLOCKS until done.
//                     Kept for tests and simple callers.
//   * speakAsync()  — STREAMING + non-blocking. Splits the text into sentence-ish
//                     chunks and synthesizes each while the previous one is still
//                     playing, so the first words start as soon as the first
//                     sentence is ready (low time-to-first-word on long answers).
//                     Runs on its own playback thread and returns immediately;
//                     stop() aborts it ASAP (this is what powers barge-in in
//                     AudioService, which cuts playback when it hears the wake
//                     word over the assistant's own voice).
//
#include <filesystem>
#include <functional>
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

    // Streaming, non-blocking playback. Cancels any current playback, then spins
    // a worker that chunks `text` into sentences and synthesizes+plays them in
    // order, overlapping synthesis of later chunks with playback of earlier ones.
    // Returns immediately. The finished-callback (if set) fires from the worker
    // thread when playback ends on its own — NOT when stop() cancels it.
    void speakAsync(const std::string& text, const std::string& voice);

    // Aborts speakAsync() playback as fast as possible and joins the worker.
    // Safe to call when nothing is playing. The finished-callback does NOT fire.
    void stop();

    // True while a speakAsync() worker is actively synthesizing/playing.
    bool isSpeaking() const;

    // Invoked (from the worker thread) when speakAsync() playback completes
    // naturally or fails to start — i.e. every end that the caller did not ask
    // for via stop(). Lets the caller reset its "speaking" state. Set once.
    void setFinishedCallback(std::function<void()> cb);

    // Synthesis only (no playback) — exposed for tests / saving to file.
    bool synthesize(const std::string& text, const std::string& voice,
                    std::vector<int16_t>& out_pcm, int& out_sample_rate);

    // Splits text into sentence-ish chunks for streaming synthesis. Pure logic
    // (no audio), exposed for tests. Adjacent fragments are merged until they
    // reach `min_chars`; very long runs are broken at a space near `max_chars`.
    static std::vector<std::string> splitForStreaming(const std::string& text,
                                                       size_t min_chars = 60,
                                                       size_t max_chars = 240);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    bool ready_ = false;
};

} // namespace polymath::audio
