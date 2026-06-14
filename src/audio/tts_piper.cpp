#include "tts_piper.h"
#include "logging.h"

#include <QProcess>
#include <QStringList>
#include <QByteArray>

#include <nlohmann/json.hpp>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>

// miniaudio implementation lives in capture.cpp; here we only use the API.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

// TTS via the prebuilt Piper engine (piper.exe), driven as a subprocess:
//   text -> stdin ; raw s16 mono PCM -> stdout.
// This avoids building libpiper/piper-phonemize/espeak-ng from source (a painful
// chain on Windows). The engine (piper.exe + dlls + espeak-ng-data) is deployed
// to <data>/models/piper-engine/; voices live in <data>/models/piper/<voice>/.

namespace polymath::audio {

struct TtsPiper::Impl {
    std::filesystem::path voices_dir;
    std::string           default_voice;
    std::filesystem::path piper_exe;     // models/piper-engine/piper.exe

    // --- streaming playback (speakAsync) ---------------------------------------
    std::thread            worker;            // synth+play pipeline thread
    std::mutex             ctl;               // serialises speakAsync()/stop()
    std::atomic<bool>      abort{false};      // request the worker to bail out
    std::atomic<bool>      speaking{false};   // worker active
    std::function<void()>  on_finished;       // natural-end callback (set once)

    // Read a voice's sample rate from its .onnx.json (Piper config), default 22050.
    int sampleRate(const std::filesystem::path& cfg) const {
        try {
            std::ifstream in(cfg);
            auto j = nlohmann::json::parse(in);
            if (j.contains("audio") && j["audio"].contains("sample_rate"))
                return j["audio"]["sample_rate"].get<int>();
        } catch (...) {}
        return 22050;
    }
};

TtsPiper::TtsPiper() : d_(std::make_unique<Impl>()) {}
TtsPiper::~TtsPiper() { stop(); }

bool TtsPiper::init(const std::filesystem::path& voices_dir, const std::string& default_voice) {
    d_->voices_dir    = voices_dir;
    d_->default_voice = default_voice;
    // Engine sits next to the voices: <models>/piper-engine/piper.exe.
    d_->piper_exe = voices_dir.parent_path() / "piper-engine" / "piper.exe";
    ready_ = std::filesystem::exists(d_->piper_exe);
    if (ready_)
        PM_INFO("audio.tts: Piper engine at {} (default voice '{}')",
                d_->piper_exe.string(), default_voice);
    else
        PM_WARN("audio.tts: piper.exe not found at {} — TTS disabled "
                "(run scripts/fetch-models.ps1)", d_->piper_exe.string());
    return ready_;
}

bool TtsPiper::synthesize(const std::string& text, const std::string& voice,
                          std::vector<int16_t>& out_pcm, int& out_sample_rate) {
    out_pcm.clear();
    out_sample_rate = 22050;
    if (!ready_ || text.empty()) return false;

    // Resolve the voice files, falling back to the default voice.
    auto resolve = [&](const std::string& name) -> std::filesystem::path {
        auto model = d_->voices_dir / name / (name + ".onnx");
        return std::filesystem::exists(model) ? model : std::filesystem::path{};
    };
    std::string name = voice.empty() ? d_->default_voice : voice;
    std::filesystem::path model = resolve(name);
    if (model.empty() && name != d_->default_voice) { name = d_->default_voice; model = resolve(name); }
    if (model.empty()) {
        PM_WARN("audio.tts: voice '{}' not found under {}", name, d_->voices_dir.string());
        return false;
    }
    const auto cfg = d_->voices_dir / name / (name + ".onnx.json");
    out_sample_rate = d_->sampleRate(cfg);

    // Run piper.exe: text on stdin, raw s16 mono PCM on stdout. The working dir
    // is the engine folder so it locates espeak-ng-data/ + its DLLs.
    QProcess proc;
    proc.setWorkingDirectory(QString::fromStdString(d_->piper_exe.parent_path().string()));
    proc.setProgram(QString::fromStdString(d_->piper_exe.string()));
    proc.setArguments(QStringList{}
        << "--model"  << QString::fromStdString(model.string())
        << "--config" << QString::fromStdString(cfg.string())
        << "--output_raw");
    proc.start();
    if (!proc.waitForStarted(5000)) {
        PM_ERROR("audio.tts: failed to start piper.exe");
        return false;
    }
    proc.write(text.data(), static_cast<qint64>(text.size()));
    proc.closeWriteChannel();
    if (!proc.waitForFinished(30000)) {
        PM_ERROR("audio.tts: piper.exe timed out");
        proc.kill();
        return false;
    }
    const QByteArray raw = proc.readAllStandardOutput();
    if (raw.isEmpty()) {
        PM_ERROR("audio.tts: piper produced no audio ({})",
                 QString::fromUtf8(proc.readAllStandardError().left(200)).toStdString());
        return false;
    }
    out_pcm.resize(static_cast<size_t>(raw.size()) / sizeof(int16_t));
    std::memcpy(out_pcm.data(), raw.constData(), out_pcm.size() * sizeof(int16_t));
    return !out_pcm.empty();
}

bool TtsPiper::speak(const std::string& text, const std::string& voice) {
    std::vector<int16_t> pcm;
    int sr = 22050;
    if (!synthesize(text, voice, pcm, sr) || pcm.empty()) return false;

    // Blocking playback via a temporary miniaudio device + a tiny cursor.
    struct PlaybackState {
        const int16_t* data = nullptr;
        size_t         total = 0;
        size_t         pos = 0;
        bool           done = false;
    } state;
    state.data  = pcm.data();
    state.total = pcm.size();

    auto cb = [](ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames) {
        auto* st = static_cast<PlaybackState*>(dev->pUserData);
        auto* dst = static_cast<int16_t*>(out);
        ma_uint32 i = 0;
        for (; i < frames && st->pos < st->total; ++i, ++st->pos)
            dst[i] = st->data[st->pos];
        for (; i < frames; ++i) dst[i] = 0;   // pad tail with silence
        if (st->pos >= st->total) st->done = true;
    };

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_s16;
    cfg.playback.channels = 1;
    cfg.sampleRate        = static_cast<ma_uint32>(sr);
    cfg.dataCallback      = cb;
    cfg.pUserData         = &state;

    ma_device device;
    if (ma_device_init(nullptr, &cfg, &device) != MA_SUCCESS) {
        PM_ERROR("audio.tts: playback device init failed");
        return false;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        PM_ERROR("audio.tts: playback start failed");
        return false;
    }

    while (!state.done) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));   // flush last period

    ma_device_uninit(&device);
    PM_DEBUG("audio.tts: spoke {} samples @ {} Hz", pcm.size(), sr);
    return true;
}

// --- streaming playback (speakAsync) ---------------------------------------

std::vector<std::string> TtsPiper::splitForStreaming(const std::string& text,
                                                     size_t min_chars,
                                                     size_t max_chars) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        const size_t b = cur.find_first_not_of(" \t\r\n");
        const size_t e = cur.find_last_not_of(" \t\r\n");
        if (b != std::string::npos) out.emplace_back(cur.substr(b, e - b + 1));
        cur.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        cur.push_back(c);
        const bool eos = (c == '.' || c == '!' || c == '?' || c == '\n');
        const bool boundary =
            eos && (i + 1 >= text.size() ||
                    std::isspace(static_cast<unsigned char>(text[i + 1])));
        if (boundary && cur.size() >= min_chars) { flush(); continue; }
        // Hard cap: break a runaway sentence at the next space past max_chars so
        // a wall of text still streams (and Piper isn't handed an enormous line).
        if (cur.size() >= max_chars && std::isspace(static_cast<unsigned char>(c)))
            flush();
    }
    flush();
    return out;   // empty only for empty/all-whitespace input
}

namespace {

// Shared state between the producer (runStream) and the miniaudio playback
// callback. The producer pushes synthesized PCM chunks; the callback drains them.
struct StreamState {
    std::mutex                       m;
    std::deque<std::vector<int16_t>> chunks;     // ready PCM, FIFO
    size_t                           cursor = 0;  // read pos within chunks.front()
    bool                             producer_done = false;
    bool                             drained = false;  // callback hit the end
    std::atomic<bool>*               abort = nullptr;
};

void streamCallback(ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames) {
    auto* st  = static_cast<StreamState*>(dev->pUserData);
    auto* dst = static_cast<int16_t*>(out);
    ma_uint32 i = 0;
    if (st->abort && st->abort->load(std::memory_order_acquire)) {
        for (; i < frames; ++i) dst[i] = 0;
        std::lock_guard<std::mutex> lk(st->m);
        st->drained = true;
        return;
    }
    std::lock_guard<std::mutex> lk(st->m);
    while (i < frames && !st->chunks.empty()) {
        auto& front = st->chunks.front();
        for (; i < frames && st->cursor < front.size(); ++i, ++st->cursor)
            dst[i] = front[st->cursor];
        if (st->cursor >= front.size()) { st->chunks.pop_front(); st->cursor = 0; }
    }
    for (; i < frames; ++i) dst[i] = 0;            // underrun / tail -> silence
    if (st->chunks.empty() && st->producer_done) st->drained = true;
}

// The producer: chunk the text, synthesize each chunk (overlapping later chunks
// with playback of earlier ones via the shared queue) and feed the device until
// it has drained or `abort` is set.
void runStream(TtsPiper& tts, std::atomic<bool>& abort,
               const std::string& text, const std::string& voice) {
    auto chunks = TtsPiper::splitForStreaming(text);
    if (chunks.empty()) return;

    // Synthesize chunk 0 up front: its render time is the unavoidable
    // time-to-first-word, and it tells us the voice's sample rate.
    std::vector<int16_t> first;
    int sr = 22050;
    if (!tts.synthesize(chunks[0], voice, first, sr) || first.empty()) {
        PM_WARN("audio.tts: streaming synth produced no audio on first chunk");
        return;
    }
    if (abort.load(std::memory_order_acquire)) return;

    StreamState st;
    st.abort = &abort;
    st.chunks.push_back(std::move(first));

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_s16;
    cfg.playback.channels = 1;
    cfg.sampleRate        = static_cast<ma_uint32>(sr);
    cfg.dataCallback      = streamCallback;
    cfg.pUserData         = &st;

    ma_device device;
    if (ma_device_init(nullptr, &cfg, &device) != MA_SUCCESS) {
        PM_ERROR("audio.tts: streaming playback device init failed");
        return;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        PM_ERROR("audio.tts: streaming playback start failed");
        return;
    }

    // Synthesize the remaining chunks while chunk 0 (and onward) plays.
    for (size_t i = 1; i < chunks.size(); ++i) {
        if (abort.load(std::memory_order_acquire)) break;
        std::vector<int16_t> pcm;
        int csr = sr;
        if (tts.synthesize(chunks[i], voice, pcm, csr) && !pcm.empty()) {
            std::lock_guard<std::mutex> lk(st.m);
            st.chunks.push_back(std::move(pcm));
        }
    }
    { std::lock_guard<std::mutex> lk(st.m); st.producer_done = true; }

    // Wait for the callback to drain the queue (or for an abort).
    for (;;) {
        if (abort.load(std::memory_order_acquire)) break;
        bool done;
        { std::lock_guard<std::mutex> lk(st.m); done = st.drained; }
        if (done) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!abort.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(60));  // flush last period

    ma_device_uninit(&device);   // stops the device + joins its thread (no more cb)
}

} // namespace

void TtsPiper::speakAsync(const std::string& text, const std::string& voice) {
    std::lock_guard<std::mutex> lk(d_->ctl);
    // Cancel anything currently playing first.
    if (d_->worker.joinable()) {
        d_->abort.store(true, std::memory_order_release);
        d_->worker.join();
    }
    d_->abort.store(false, std::memory_order_release);

    if (!ready_ || text.empty()) {
        d_->speaking.store(false, std::memory_order_release);
        if (d_->on_finished) d_->on_finished();   // let the caller clear its state
        return;
    }

    d_->speaking.store(true, std::memory_order_release);
    Impl* d = d_.get();
    const std::string txt = text, v = voice;
    d_->worker = std::thread([this, d, txt, v]() {
        runStream(*this, d->abort, txt, v);
        d->speaking.store(false, std::memory_order_release);
        // Natural end (incl. synth failure) notifies; an abort (stop()) does not,
        // since the caller that asked for the stop already knows.
        if (!d->abort.load(std::memory_order_acquire) && d->on_finished)
            d->on_finished();
    });
}

void TtsPiper::stop() {
    std::lock_guard<std::mutex> lk(d_->ctl);
    if (d_->worker.joinable()) {
        d_->abort.store(true, std::memory_order_release);
        d_->worker.join();
    }
    d_->abort.store(false, std::memory_order_release);
    d_->speaking.store(false, std::memory_order_release);
}

bool TtsPiper::isSpeaking() const {
    return d_->speaking.load(std::memory_order_acquire);
}

void TtsPiper::setFinishedCallback(std::function<void()> cb) {
    d_->on_finished = std::move(cb);
}

} // namespace polymath::audio
