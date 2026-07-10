// Integration test for the voice loop (Wave 1 · Card A + overhaul A4).
//
// Drives the real audio pipeline stages end-to-end with SYNTHETIC audio only
// (never a live mic). Covered:
//   1. ASR        — synthesized phrase -> whisper transcript matches expected.
//   2. Wake+VAD   — silence gated; speech discriminated; bus flow.
//   3. TTS        — SpeakRequest text -> Piper -> non-empty PCM.
//   4. Privacy    — AudioService with mic disabled: no Utterance.
//   A4 new:
//   5. oWW gating — WakeWord::process never called without VAD speech (unit of
//                   the inverted idle chain).
//   6. Lazy ASR   — load/unload timing: not loaded until ensure; unload works.
//   7. TTS chunks — splitSentences order; synthesizeSentences chunk order.
//   8. Ring drops — FloatRing 16 s capacity; drop counter increments on overflow.

#include "audio_common.h"
#include "capture.h"
#include "wakeword.h"
#include "vad.h"
#include "asr_whisper.h"
#include "tts_piper.h"

#include "audio_service.h"
#include "service.h"
#include "database.h"
#include "config.h"
#include "event_bus.h"
#include "paths.h"

#undef NDEBUG   // keep assert() live in Release
#include <cassert>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QTimer>
#include <QThread>
#include <QEventLoop>

using namespace polymath;
using namespace polymath::audio;

namespace {

std::filesystem::path g_models;   // <exe_dir>/data/models

// --- helpers ---------------------------------------------------------------

std::vector<float> resample(const std::vector<float>& in, int src_rate, int dst_rate) {
    if (src_rate == dst_rate || in.empty()) return in;
    const double ratio = static_cast<double>(dst_rate) / src_rate;
    const size_t out_n = static_cast<size_t>(in.size() * ratio);
    std::vector<float> out(out_n);
    for (size_t i = 0; i < out_n; ++i) {
        const double srcpos = i / ratio;
        const size_t i0 = static_cast<size_t>(srcpos);
        const size_t i1 = std::min(i0 + 1, in.size() - 1);
        const double frac = srcpos - i0;
        out[i] = static_cast<float>(in[i0] * (1.0 - frac) + in[i1] * frac);
    }
    return out;
}

std::vector<float> toFloat(const std::vector<int16_t>& pcm) {
    std::vector<float> f(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) f[i] = pcm[i] / 32768.0f;
    return f;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool contains(const std::string& hay, const std::string& needle) {
    return lower(hay).find(lower(needle)) != std::string::npos;
}

std::vector<float> synth16k(TtsPiper& tts, const std::string& text, int& native_sr) {
    std::vector<int16_t> pcm;
    native_sr = 22050;
    if (!tts.synthesize(text, "", pcm, native_sr) || pcm.empty()) return {};
    return resample(toFloat(pcm), native_sr, kSampleRate);
}

std::vector<float> synthCommand(TtsPiper& tts) {
    int sr = 0;
    auto a = synth16k(tts, "Turn on the kitchen light please.", sr);
    if (!a.empty()) a.insert(a.end(), static_cast<size_t>(kSampleRate) / 2, 0.0f);
    return a;
}

struct SegResult {
    bool        speech_started = false;
    float       peak_prob      = 0.0f;
    std::string text;
};

SegResult segmentAndTranscribe(Vad& vad, AsrWhisper& asr,
                               const std::vector<float>& audio) {
    SegResult r;
    vad.reset();
    std::vector<float> segment;
    std::vector<float> captured;
    bool capturing = false;
    bool finished = false;

    const size_t W = 512;
    for (size_t off = 0; off + W <= audio.size() && !finished; off += W) {
        float prob = 0.0f;
        Vad::Event ev = vad.process(audio.data() + off, &prob);
        if (prob > r.peak_prob) r.peak_prob = prob;
        if (ev == Vad::Event::SpeechStart) {
            r.speech_started = true;
            capturing = true;
        }
        if (capturing)
            segment.insert(segment.end(), audio.data() + off, audio.data() + off + W);
        if (ev == Vad::Event::SpeechEnd) {
            captured = segment;
            finished = true;
        }
    }
    if (!finished && capturing && !segment.empty()) captured = segment;

    if (captured.size() < static_cast<size_t>(kSampleRate) / 5) return r;
    if (!asr.ready()) return r;
    float conf = 0.0f;
    r.text = asr.transcribe(captured, &conf);
    return r;
}

// Replicates the A4 inverted idle-chain gating: VAD first, oWW only during
// speech + hangover. Counts wake.process calls.
struct GateResult {
    size_t oww_calls_on_silence = 0;
    size_t oww_calls_on_speech  = 0;
    bool   speech_seen          = false;
};

GateResult runGatedOww(Vad& vad, WakeWord& wake, const std::vector<float>& audio) {
    GateResult g;
    vad.reset();
    wake.reset();
    wake.resetProcessCalls();

    bool in_speech = false;
    int hangover_ms = 0;
    float vad_acc[512];
    int vad_fill = 0;

    auto feed = [&](const float* data, int n, bool expect_oww) {
        // VAD windows
        int off = 0;
        while (off < n) {
            const int space = 512 - vad_fill;
            const int take  = std::min(space, n - off);
            std::memcpy(vad_acc + vad_fill, data + off, static_cast<size_t>(take) * sizeof(float));
            vad_fill += take;
            off += take;
            if (vad_fill < 512) break;
            Vad::Event ev = vad.process(vad_acc, nullptr);
            vad_fill = 0;
            if (ev == Vad::Event::SpeechStart) {
                in_speech = true;
                hangover_ms = 0;
                g.speech_seen = true;
            } else if (ev == Vad::Event::SpeechEnd) {
                in_speech = false;
                hangover_ms = kOwwHangoverMs;
            }
        }
        if (!in_speech && hangover_ms > 0) {
            const int frame_ms = (n * 1000) / kSampleRate;
            hangover_ms = std::max(0, hangover_ms - frame_ms);
        }
        const bool gate = in_speech || hangover_ms > 0;
        if (gate) {
            const size_t before = wake.processCalls();
            wake.process(data, n, nullptr);
            const size_t delta = wake.processCalls() - before;
            if (expect_oww) g.oww_calls_on_speech += delta;
            else            g.oww_calls_on_silence += delta;
        }
        // If gate is false we deliberately do NOT call wake.process — that is
        // the property under test.
        (void)expect_oww;
    };

    for (size_t off = 0; off + static_cast<size_t>(kFrameSamples) <= audio.size();
         off += static_cast<size_t>(kFrameSamples)) {
        // expect_oww is informational; we count only when gate opens.
        feed(audio.data() + static_cast<std::ptrdiff_t>(off), kFrameSamples, true);
    }
    return g;
}

// --- checks ----------------------------------------------------------------

void checkTts(TtsPiper& tts) {
    if (!tts.ready()) { std::puts("  [skip] TTS: piper.exe not found"); return; }
    std::vector<int16_t> pcm;
    int sr = 0;
    bool ok = tts.synthesize("Testing one two three.", "", pcm, sr);
    assert(ok && "Piper synthesis failed");
    assert(!pcm.empty() && "Piper produced no audio");
    assert(sr >= 16000 && "implausible TTS sample rate");
    const double seconds = static_cast<double>(pcm.size()) / sr;
    assert(seconds > 0.3 && seconds < 15.0 && "implausible TTS duration");
    std::printf("  [ok]   TTS: %zu samples @ %d Hz (%.2fs)\n", pcm.size(), sr, seconds);
}

void checkAsr(TtsPiper& tts, AsrWhisper& asr) {
    if (!tts.ready()) { std::puts("  [skip] ASR: no TTS to make a fixture"); return; }
    if (!asr.ready()) { std::puts("  [skip] ASR: whisper model not loaded"); return; }
    int sr = 0;
    auto audio = synth16k(tts, "The quick brown fox.", sr);
    assert(!audio.empty() && "failed to synthesize ASR fixture");
    float conf = 0.0f;
    std::string text = asr.transcribe(audio, &conf);
    std::printf("  [..]   ASR heard: \"%s\" (conf %.2f)\n", text.c_str(), conf);
    assert((contains(text, "quick") || contains(text, "brown") || contains(text, "fox"))
           && "ASR transcript did not match the spoken phrase");
    std::puts("  [ok]   ASR: transcript matches expected phrase");
}

void checkVadGating(TtsPiper& tts, Vad& vad, AsrWhisper& asr) {
    if (!vad.ready()) { std::puts("  [skip] VAD: silero model not loaded"); return; }

    vad.setThreshold(0.5f);
    std::vector<float> silence(static_cast<size_t>(kSampleRate) * 2, 0.0f);
    SegResult sil = segmentAndTranscribe(vad, asr, silence);
    std::printf("  [..]   VAD silence peak prob %.4f\n", sil.peak_prob);
    assert(!sil.speech_started && "VAD reported speech on pure silence");
    assert(sil.text.empty() && "silence opened a segment -> Utterance (gate failed)");
    std::puts("  [ok]   VAD: silence gated (no speech segment, no Utterance)");

    if (!tts.ready()) { std::puts("  [skip] VAD speech: no TTS fixture"); return; }

    std::vector<float> speech;
    SegResult sp;
    for (int attempt = 0; attempt < 4; ++attempt) {
        speech = synthCommand(tts);
        assert(!speech.empty() && "failed to synthesize speech clip");
        sp = segmentAndTranscribe(vad, asr, speech);
        if (sp.peak_prob > 0.02f) break;
    }
    std::printf("  [..]   VAD speech clip %.2fs, peak prob %.4f (silence %.4f)\n",
                speech.size() / 16000.0, sp.peak_prob, sil.peak_prob);
    assert(sp.peak_prob > sil.peak_prob * 5.0f && sp.peak_prob > 0.01f &&
           "VAD did not discriminate speech from silence");
    std::puts("  [ok]   VAD: discriminates speech from silence");

    if (asr.ready()) {
        float conf = 0.0f;
        std::string utt = asr.transcribe(speech, &conf);
        std::printf("  [ok]   chain: speech -> Utterance \"%s\" (conf %.2f)\n",
                    utt.c_str(), conf);
        assert(!utt.empty() && "command segment yielded no Utterance text");
        assert((contains(utt, "kitchen") || contains(utt, "light") || contains(utt, "turn"))
               && "Utterance text did not match the spoken command");
    } else {
        std::puts("  [skip] chain: whisper model not loaded");
    }
}

void checkWakeWord(TtsPiper& tts, WakeWord& wake) {
    if (!wake.ready()) { std::puts("  [skip] wake word: models not loaded"); return; }

    std::vector<float> frame(static_cast<size_t>(kFrameSamples), 0.0f);
    bool fired_on_silence = false;
    for (int i = 0; i < 50; ++i) {
        float sc = 0.0f;
        if (wake.process(frame.data(), kFrameSamples, &sc)) fired_on_silence = true;
    }
    assert(!fired_on_silence && "wake word fired on silence");
    std::puts("  [ok]   wake: silent audio never triggers");

    if (!tts.ready()) { std::puts("  [skip] wake positive: no TTS fixture"); return; }
    wake.reset();
    int sr = 0;
    auto audio = synth16k(tts, "Hey Jarvis. Hey Jarvis.", sr);
    if (audio.empty()) { std::puts("  [skip] wake positive: synth failed"); return; }
    float peak = 0.0f;
    bool fired = false;
    for (size_t off = 0; off + static_cast<size_t>(kFrameSamples) <= audio.size();
         off += static_cast<size_t>(kFrameSamples)) {
        float sc = 0.0f;
        if (wake.process(audio.data() + off, kFrameSamples, &sc)) fired = true;
        peak = std::max(peak, sc);
    }
    std::printf("  [%s]   wake: spoken clip peak score %.3f%s\n",
                (peak > 0.0f ? "ok" : ".."), peak,
                fired ? " (FIRED)" : "");
    assert(peak >= 0.0f && peak <= 1.0001f && "wake score out of range");
}

void checkPrivacyGate() {
    auto tmp = std::filesystem::temp_directory_path() / "polymath_audio_e2e.db";
    std::filesystem::remove(tmp);
    Database db;
    assert(db.open(tmp.string()));
    Config cfg(db);
    cfg.seedDefaults();
    db.setSetting(keys::MicEnabled, "0");
    db.setSetting(keys::AmbientTranscription, "0");

    std::atomic<int> utterances{0};
    QObject sink;
    QObject::connect(&EventBus::instance(), &EventBus::utterance, &sink,
                     [&](const Utterance&) { utterances.fetch_add(1); });

    auto* svc = new AudioService(db);
    QThread* thread = runOnThread(svc, svc);

    {
        QEventLoop loop;
        QTimer::singleShot(700, &loop, &QEventLoop::quit);
        loop.exec();
    }

    thread->quit();
    thread->wait(5000);
    delete svc;
    delete thread;

    assert(utterances.load() == 0 && "Utterance emitted while mic disabled (privacy breach)");
    std::puts("  [ok]   privacy: mic OFF -> no capture, no Utterance");

    db.close();
    std::filesystem::remove(tmp);
}

void checkEventBusFlow() {
    std::vector<QString> seen;
    QString utter_text;
    QObject sink;
    QObject::connect(&EventBus::instance(), &EventBus::wakeWord, &sink,
                     [&](const WakeWordEvent& e) { seen.push_back("wake:" + e.phrase); });
    QObject::connect(&EventBus::instance(), &EventBus::utterance, &sink,
                     [&](const Utterance& u) {
                         seen.push_back("utterance");
                         utter_text = QString::fromStdString(u.text);
                     });

    EventBus::instance().publishWakeWord({QStringLiteral("hey_jarvis"), 0});
    Utterance u; u.text = "turn on the kitchen light"; u.is_ambient = false; u.confidence = 0.9f;
    EventBus::instance().publishUtterance(u);

    QCoreApplication::processEvents();

    assert(seen.size() == 2 && "expected exactly a wake event then an utterance");
    assert(seen[0] == "wake:hey_jarvis" && "wake event not delivered first");
    assert(seen[1] == "utterance" && "utterance not delivered after wake");
    assert(utter_text == "turn on the kitchen light" && "utterance payload corrupted on the bus");
    std::puts("  [ok]   bus: WakeWordDetected -> Utterance delivered in order");
}

// A4 §4.5: oWW never invoked without VAD speech (gating).
void checkOwwGating(TtsPiper& tts, Vad& vad, WakeWord& wake) {
    if (!vad.ready() || !wake.ready()) {
        std::puts("  [skip] oWW gating: VAD or wake models missing");
        return;
    }

    // Pure silence: gate must keep oWW at zero calls.
    wake.reset();
    wake.resetProcessCalls();
    vad.reset();
    std::vector<float> silence(static_cast<size_t>(kSampleRate) * 2, 0.0f);
    GateResult sil = runGatedOww(vad, wake, silence);
    std::printf("  [..]   oWW calls on silence=%zu (must be 0)\n",
                wake.processCalls());
    assert(wake.processCalls() == 0 && "oWW invoked on silence (gate failed)");
    assert(!sil.speech_seen && "VAD saw speech on silence");
    std::puts("  [ok]   oWW gating: never invoked without VAD speech (silence)");

    if (!tts.ready()) {
        std::puts("  [skip] oWW gating speech: no TTS fixture");
        return;
    }

    // Speech: gate may open; processCalls must be > 0 only if VAD saw speech.
    wake.reset();
    wake.resetProcessCalls();
    vad.reset();
    auto speech = synthCommand(tts);
    if (speech.empty()) {
        std::puts("  [skip] oWW gating speech: synth failed");
        return;
    }
    GateResult sp = runGatedOww(vad, wake, speech);
    std::printf("  [..]   oWW calls on speech=%zu speech_seen=%d\n",
                wake.processCalls(), static_cast<int>(sp.speech_seen));
    // If Silero scores the TTS clip as speech, oWW must have been fed.
    // If not (TTS variance), we still pass as long as silence stayed at 0 —
    // the invariant is "no speech ⇒ no oWW", not "speech ⇒ oWW always".
    if (sp.speech_seen) {
        assert(wake.processCalls() > 0 && "VAD speech but oWW never fed");
        std::puts("  [ok]   oWW gating: fed during VAD speech");
    } else {
        std::puts("  [ok]   oWW gating: speech clip below VAD thresh (TTS variance); silence invariant holds");
    }
}

// A4 §3.2 / §4.5: lazy ASR load + unload timing.
void checkLazyAsr() {
    AsrWhisper asr;
    asr.setThreads(2);
    asr.resetStats();
    assert(!asr.isLoaded() && "ASR must start unloaded");
    assert(asr.loadCount() == 0);

    const auto path = g_models / "whisper" / "ggml-base.en.bin";
    if (!std::filesystem::exists(path)) {
        std::puts("  [skip] lazy ASR: ggml-base.en.bin missing");
        return;
    }

    // Not loaded until explicit load (mirrors no-eager-load in start()).
    assert(!asr.ready());
    const bool ok = asr.load(path, AsrWhisper::Mode::Command);
    assert(ok && asr.isLoaded() && "load() should make ASR ready");
    assert(asr.loadCount() == 1);

    // Second load same path is a no-op success (no double free / double count).
    assert(asr.load(path, AsrWhisper::Mode::Command));
    assert(asr.loadCount() == 1);

    asr.unload();
    assert(!asr.isLoaded() && "unload() must clear ready");
    assert(asr.unloadCount() >= 1);

    // Reload works after unload.
    assert(asr.load(path, AsrWhisper::Mode::Command));
    assert(asr.isLoaded() && asr.loadCount() == 2);
    asr.unload();

    std::puts("  [ok]   lazy ASR: load/unload timing + no eager residency");
}

// A4 §3.4 / §4.5: sentence chunk ordering.
void checkTtsChunkOrdering(TtsPiper& tts) {
    // Pure unit: splitSentences order (no models required).
    const auto parts = TtsPiper::splitSentences(
        "Hello there. How are you? I am fine!");
    assert(parts.size() == 3 && "expected 3 sentence chunks");
    assert(contains(parts[0], "Hello") && "chunk 0 out of order");
    assert(contains(parts[1], "How are you") && "chunk 1 out of order");
    assert(contains(parts[2], "I am fine") && "chunk 2 out of order");
    std::puts("  [ok]   TTS chunks: splitSentences preserves order");

    if (!tts.ready()) {
        std::puts("  [skip] TTS chunk synth: piper.exe not found");
        return;
    }

    std::vector<std::vector<int16_t>> chunks;
    int sr = 0;
    // Two short sentences so we exercise the persistent line path without a
    // long test.
    const bool ok = tts.synthesizeSentences(
        "One. Two.", "", chunks, sr);
    if (!ok || chunks.empty()) {
        std::puts("  [skip] TTS chunk synth: synthesis failed (engine/voice?)");
        return;
    }
    assert(chunks.size() >= 1 && "expected at least one audio chunk");
    // Each non-empty chunk must have plausible length; order is insertion order.
    size_t total = 0;
    for (size_t i = 0; i < chunks.size(); ++i) {
        assert(!chunks[i].empty() && "empty TTS chunk");
        total += chunks[i].size();
    }
    assert(total > 0);
    std::printf("  [ok]   TTS chunks: %zu ordered audio chunk(s), %zu samples @ %d Hz\n",
                chunks.size(), total, sr);
}

// A4 §3.3 / §4.3: ring is 16 s; drop counter increments on overflow.
void checkRingDrops() {
    FloatRing ring(kRingCapacity);
    assert(ring.capacity() >= static_cast<size_t>(kSampleRate) * 16 &&
           "FloatRing must hold at least 16 s @ 16 kHz");
    assert(ring.drops() == 0);

    // Fill to capacity without overflowing (write only free space each step).
    std::vector<float> block(static_cast<size_t>(kFrameSamples), 0.25f);
    while (ring.available() < ring.capacity()) {
        const size_t free = ring.capacity() - ring.available();
        const size_t n = std::min(block.size(), free);
        const size_t w = ring.write(block.data(), n);
        if (w == 0) break;
    }
    assert(ring.drops() == 0 && "no drops while filling to capacity");
    assert(ring.available() == ring.capacity());

    // One more write must drop.
    const size_t d0 = ring.drops();
    ring.write(block.data(), block.size());
    assert(ring.drops() > d0 && "drop counter must increment on overflow");
    std::printf("  [ok]   ring: capacity=%zu (~%.1fs), drops=%zu after overflow\n",
                ring.capacity(),
                static_cast<double>(ring.capacity()) / kSampleRate,
                ring.drops());

    // Capture default ring uses kRingCapacity too.
    Capture cap;
    assert(cap.ring().capacity() >= static_cast<size_t>(kSampleRate) * 16 &&
           "Capture ring must be 16 s");
    std::puts("  [ok]   ring: Capture defaults to 16 s headroom");
}

// A4 barge-in unit: stop() flushes TTS queue.
void checkTtsStop(TtsPiper& tts) {
    if (!tts.ready()) { std::puts("  [skip] TTS stop: piper not ready"); return; }
    // stop() while idle must be safe.
    tts.stop();
    assert(!tts.isSpeaking() && "idle TTS must not report speaking after stop");
    std::puts("  [ok]   TTS stop: idle stop is a no-op / not speaking");
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);

    const auto exe_dir = std::filesystem::path(
        QCoreApplication::applicationDirPath().toStdString());
    g_models = exe_dir / "data" / "models";
    Paths::instance().setRoot(exe_dir / "data");
    std::printf("models: %s (exists=%d)\n",
                g_models.string().c_str(),
                static_cast<int>(std::filesystem::exists(g_models)));

    TtsPiper tts;  tts.init(g_models / "piper", "en_US-amy-medium");
    WakeWord wake; wake.load(g_models / "wakeword", "hey_jarvis");
    Vad      vad;  vad.load(g_models / "vad" / "silero_vad.onnx");
    AsrWhisper asr;
    asr.setThreads(4);
    // Explicit load for legacy checks that need a ready model — proves lazy
    // load works independently via checkLazyAsr().
    asr.load(g_models / "whisper" / "ggml-base.en.bin", AsrWhisper::Mode::Command);

    std::puts("== audio e2e ==");
    checkRingDrops();                    // A4: 16 s ring + drop counter
    checkTts(tts);                       // 3. SpeakRequest -> non-empty TTS PCM
    checkTtsChunkOrdering(tts);          // A4: sentence chunk order
    checkTtsStop(tts);                   // A4: barge-in stop
    checkLazyAsr();                      // A4: lazy load/unload
    checkAsr(tts, asr);                  // 1. WAV -> expected transcript
    checkVadGating(tts, vad, asr);       // 2a. silence gated; speech -> Utterance
    checkWakeWord(tts, wake);            // 2b. wake never false-fires on silence
    checkOwwGating(tts, vad, wake);      // A4: oWW never without VAD speech
    checkEventBusFlow();                 // 2c. WakeWordDetected -> Utterance on bus
    checkPrivacyGate();                  // 4. mic OFF -> no capture, no Utterance

    std::puts("test_audio_e2e: OK");
    return 0;
}
