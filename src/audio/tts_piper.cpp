#include "tts_piper.h"
#include "logging.h"

#include <QProcess>
#include <QStringList>
#include <QByteArray>
#include <QElapsedTimer>

#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

// miniaudio implementation lives in capture.cpp; here we only use the API.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

// TTS via the prebuilt Piper engine (piper.exe), driven as a *persistent*
// subprocess: text lines on stdin, raw s16 mono PCM on stdout. Sentence
// chunking streams audio to a persistent miniaudio playback device.

namespace polymath::audio {

namespace {

// How long to wait for more raw PCM after a write before declaring the
// utterance complete (piper blocks on the next stdin line when done).
// First-byte budget is larger: persistent piper still pays espeak load on the
// very first synth after spawn, which used to look like "produced no audio".
constexpr int kUtteranceIdleMs    = 220;
constexpr int kFirstByteMs        = 12000;
constexpr int kUtteranceMaxMs     = 90000;

bool isSentenceEnd(char c) {
    return c == '.' || c == '!' || c == '?' || c == ';' || c == '\n';
}

} // namespace

struct TtsPiper::Impl {
    std::filesystem::path voices_dir;
    std::string           default_voice;
    std::filesystem::path piper_exe;
    std::string           output_device;

    // Persistent piper process (one voice at a time).
    std::unique_ptr<QProcess> proc;
    std::string               proc_voice;   // voice currently loaded in proc
    std::mutex                proc_mu;

    // Playback device + queue.
    ma_context play_ctx{};
    bool       play_ctx_ready = false;
    ma_device  play_dev{};
    bool       play_ready = false;
    int        play_sr    = 0;
    std::string play_dev_name;

    std::mutex play_mu;
    std::deque<std::vector<int16_t>> queue;
    size_t     pos_in_front = 0;
    std::atomic<bool> cancel{false};
    std::atomic<bool> synth_active{false};
    std::atomic<bool> play_active{false};

    int sampleRate(const std::filesystem::path& cfg) const {
        try {
            std::ifstream in(cfg);
            auto j = nlohmann::json::parse(in);
            if (j.contains("audio") && j["audio"].contains("sample_rate"))
                return j["audio"]["sample_rate"].get<int>();
        } catch (...) {}
        return 22050;
    }

    std::filesystem::path resolveModel(const std::string& name) const {
        auto model = voices_dir / name / (name + ".onnx");
        return std::filesystem::exists(model) ? model : std::filesystem::path{};
    }

    // Caller must hold proc_mu (non-recursive).
    void killProcUnlocked() {
        if (!proc) return;
        if (proc->state() != QProcess::NotRunning) {
            proc->kill();
            proc->waitForFinished(2000);
        }
        proc.reset();
        proc_voice.clear();
    }

    void killProc() {
        std::lock_guard<std::mutex> lock(proc_mu);
        killProcUnlocked();
    }

    // Ensure a live piper process for `voice`. Restarts on voice change / crash.
    bool ensureProc(const std::string& voice) {
        std::lock_guard<std::mutex> lock(proc_mu);
        if (proc && proc->state() == QProcess::Running && proc_voice == voice)
            return true;

        killProcUnlocked();

        std::string name = voice.empty() ? default_voice : voice;
        auto model = resolveModel(name);
        if (model.empty() && name != default_voice) {
            name = default_voice;
            model = resolveModel(name);
        }
        if (model.empty()) {
            PM_WARN("audio.tts: voice '{}' not found under {}", name, voices_dir.string());
            return false;
        }
        const auto cfg = voices_dir / name / (name + ".onnx.json");

        proc = std::make_unique<QProcess>();
        proc->setWorkingDirectory(
            QString::fromStdString(piper_exe.parent_path().string()));
        proc->setProgram(QString::fromStdString(piper_exe.string()));
        proc->setArguments(QStringList{}
            << "--model"  << QString::fromStdString(model.string())
            << "--config" << QString::fromStdString(cfg.string())
            << "--output_raw");
        proc->setProcessChannelMode(QProcess::SeparateChannels);
        proc->start();
        if (!proc->waitForStarted(5000)) {
            PM_ERROR("audio.tts: failed to start piper.exe");
            proc.reset();
            return false;
        }
        proc_voice = name;
        PM_INFO("audio.tts: persistent piper started (voice '{}')", name);
        return true;
    }

    // Write one line of text and collect the raw PCM until idle.
    bool synthLine(const std::string& line, std::vector<int16_t>& out) {
        out.clear();
        if (line.empty()) return true;
        if (!proc || proc->state() != QProcess::Running) return false;

        // Drain stale stderr so load banners don't confuse diagnostics.
        proc->readAllStandardError();

        QByteArray payload = QByteArray::fromStdString(line);
        if (!payload.endsWith('\n')) payload.append('\n');
        if (proc->write(payload) < 0) {
            PM_ERROR("audio.tts: write to piper stdin failed");
            return false;
        }
        proc->waitForBytesWritten(2000);

        QByteArray raw;
        QElapsedTimer timer;
        timer.start();
        qint64 last_data_ms = -1;

        while (timer.elapsed() < kUtteranceMaxMs) {
            if (cancel.load(std::memory_order_acquire)) return false;
            if (proc->state() != QProcess::Running) {
                // Crash mid-utterance — absorb whatever we got.
                raw += proc->readAllStandardOutput();
                break;
            }
            if (proc->waitForReadyRead(40)) {
                raw += proc->readAllStandardOutput();
                last_data_ms = timer.elapsed();
            } else {
                // Drain anything already buffered.
                const QByteArray more = proc->readAllStandardOutput();
                if (!more.isEmpty()) {
                    raw += more;
                    last_data_ms = timer.elapsed();
                } else if (last_data_ms >= 0 &&
                           (timer.elapsed() - last_data_ms) >= kUtteranceIdleMs) {
                    break;   // utterance complete
                } else if (last_data_ms < 0 && timer.elapsed() > kFirstByteMs) {
                    // Cold start never produced PCM — fail so caller can restart.
                    break;
                }
            }
            // Keep stderr drained (piper logs "Loaded voice" there).
            if (proc->bytesAvailable() == 0)
                proc->readAllStandardError();
        }

        if (raw.isEmpty()) {
            const QByteArray err = proc->readAllStandardError().left(200);
            PM_ERROR("audio.tts: piper produced no audio ({})",
                     QString::fromUtf8(err).toStdString());
            return false;
        }
        // Piper may emit an odd trailing byte; drop incomplete sample.
        const size_t n = static_cast<size_t>(raw.size()) / sizeof(int16_t);
        out.resize(n);
        std::memcpy(out.data(), raw.constData(), n * sizeof(int16_t));
        return !out.empty();
    }

    // One-shot fallback (non-persistent) used when the persistent path fails.
    bool synthOnce(const std::string& text, const std::string& voice,
                   std::vector<int16_t>& out, int& sr) {
        out.clear();
        std::string name = voice.empty() ? default_voice : voice;
        auto model = resolveModel(name);
        if (model.empty() && name != default_voice) {
            name = default_voice;
            model = resolveModel(name);
        }
        if (model.empty()) return false;
        const auto cfg = voices_dir / name / (name + ".onnx.json");
        sr = sampleRate(cfg);

        QProcess p;
        p.setWorkingDirectory(QString::fromStdString(piper_exe.parent_path().string()));
        p.setProgram(QString::fromStdString(piper_exe.string()));
        p.setArguments(QStringList{}
            << "--model"  << QString::fromStdString(model.string())
            << "--config" << QString::fromStdString(cfg.string())
            << "--output_raw");
        p.start();
        if (!p.waitForStarted(5000)) return false;
        p.write(text.data(), static_cast<qint64>(text.size()));
        p.closeWriteChannel();
        if (!p.waitForFinished(30000)) { p.kill(); return false; }
        const QByteArray raw = p.readAllStandardOutput();
        if (raw.isEmpty()) return false;
        out.resize(static_cast<size_t>(raw.size()) / sizeof(int16_t));
        std::memcpy(out.data(), raw.constData(), out.size() * sizeof(int16_t));
        return !out.empty();
    }

    static void playCallback(ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(dev->pUserData);
        auto* dst  = static_cast<int16_t*>(out);
        ma_uint32 filled = 0;

        std::lock_guard<std::mutex> lock(self->play_mu);
        while (filled < frames) {
            if (self->queue.empty()) {
                // Underrun: pad silence.
                std::memset(dst + filled, 0, (frames - filled) * sizeof(int16_t));
                self->play_active.store(false, std::memory_order_release);
                return;
            }
            auto& front = self->queue.front();
            const size_t remain = front.size() - self->pos_in_front;
            const size_t need   = static_cast<size_t>(frames - filled);
            const size_t take   = remain < need ? remain : need;
            std::memcpy(dst + filled, front.data() + self->pos_in_front,
                        take * sizeof(int16_t));
            self->pos_in_front += take;
            filled += static_cast<ma_uint32>(take);
            if (self->pos_in_front >= front.size()) {
                self->queue.pop_front();
                self->pos_in_front = 0;
            }
        }
        self->play_active.store(!self->queue.empty() || self->pos_in_front > 0,
                                std::memory_order_release);
    }

    bool ensurePlayback(int sr) {
        if (play_ready && play_sr == sr && play_dev_name == output_device)
            return true;

        if (play_ready) {
            ma_device_uninit(&play_dev);
            std::memset(&play_dev, 0, sizeof(play_dev));
            play_ready = false;
        }

        if (!play_ctx_ready) {
            ma_backend backends[] = { ma_backend_wasapi };
            if (ma_context_init(backends, 1, nullptr, &play_ctx) != MA_SUCCESS) {
                if (ma_context_init(nullptr, 0, nullptr, &play_ctx) != MA_SUCCESS) {
                    PM_ERROR("audio.tts: playback context init failed");
                    return false;
                }
            }
            play_ctx_ready = true;
        }

        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format   = ma_format_s16;
        cfg.playback.channels = 1;
        cfg.sampleRate        = static_cast<ma_uint32>(sr);
        cfg.dataCallback      = playCallback;
        cfg.pUserData         = this;

        ma_device_id chosen{};
        if (!output_device.empty()) {
            ma_device_info* pPlay = nullptr; ma_uint32 nPlay = 0;
            ma_device_info* pCap  = nullptr; ma_uint32 nCap  = 0;
            if (ma_context_get_devices(&play_ctx, &pPlay, &nPlay, &pCap, &nCap) == MA_SUCCESS) {
                std::string needle = output_device;
                std::transform(needle.begin(), needle.end(), needle.begin(),
                               [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                for (ma_uint32 i = 0; i < nPlay; ++i) {
                    std::string nm = pPlay[i].name;
                    std::transform(nm.begin(), nm.end(), nm.begin(),
                                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                    if (nm.find(needle) != std::string::npos) {
                        chosen = pPlay[i].id;
                        cfg.playback.pDeviceID = &chosen;
                        PM_INFO("audio.tts: selecting playback device '{}'", pPlay[i].name);
                        break;
                    }
                }
            }
        }

        if (ma_device_init(&play_ctx, &cfg, &play_dev) != MA_SUCCESS) {
            PM_ERROR("audio.tts: playback device init failed");
            return false;
        }

        if (ma_device_start(&play_dev) != MA_SUCCESS) {
            ma_device_uninit(&play_dev);
            PM_ERROR("audio.tts: playback start failed");
            return false;
        }
        play_ready = true;
        play_sr = sr;
        play_dev_name = output_device;
        return true;
    }

    void enqueue(std::vector<int16_t>&& pcm) {
        if (pcm.empty()) return;
        std::lock_guard<std::mutex> lock(play_mu);
        queue.push_back(std::move(pcm));
        play_active.store(true, std::memory_order_release);
    }

    void clearQueue() {
        std::lock_guard<std::mutex> lock(play_mu);
        queue.clear();
        pos_in_front = 0;
        play_active.store(false, std::memory_order_release);
    }

    bool queueEmpty() {
        std::lock_guard<std::mutex> lock(play_mu);
        return queue.empty() && pos_in_front == 0;
    }

    void waitUntilDrained() {
        while (!cancel.load(std::memory_order_acquire)) {
            if (queueEmpty() && !synth_active.load(std::memory_order_acquire))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // Small tail flush so the last period actually leaves the DAC.
        if (!cancel.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
};

TtsPiper::TtsPiper() : d_(std::make_unique<Impl>()) {}

TtsPiper::~TtsPiper() {
    stop();
    if (d_->play_ready) {
        ma_device_uninit(&d_->play_dev);
        d_->play_ready = false;
    }
    if (d_->play_ctx_ready) {
        ma_context_uninit(&d_->play_ctx);
        d_->play_ctx_ready = false;
    }
    d_->killProc();
}

bool TtsPiper::init(const std::filesystem::path& voices_dir,
                    const std::string& default_voice,
                    const std::string& output_device) {
    d_->voices_dir    = voices_dir;
    d_->default_voice = default_voice;
    d_->output_device = output_device;
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

void TtsPiper::setOutputDevice(const std::string& name) {
    d_->output_device = name;
}

std::vector<std::string> TtsPiper::splitSentences(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        // Trim whitespace.
        size_t b = 0, e = cur.size();
        while (b < e && std::isspace(static_cast<unsigned char>(cur[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(cur[e - 1]))) --e;
        if (e > b) out.emplace_back(cur.substr(b, e - b));
        cur.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        cur.push_back(c);
        if (isSentenceEnd(c)) {
            // Consume trailing quotes/brackets that belong to the sentence.
            while (i + 1 < text.size() &&
                   (text[i + 1] == '"' || text[i + 1] == '\'' || text[i + 1] == ')')) {
                cur.push_back(text[++i]);
            }
            flush();
        }
    }
    flush();
    // If nothing split (no terminator), keep the whole text as one chunk.
    if (out.empty() && !text.empty()) {
        std::string t = text;
        size_t b = 0, e = t.size();
        while (b < e && std::isspace(static_cast<unsigned char>(t[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(t[e - 1]))) --e;
        if (e > b) out.emplace_back(t.substr(b, e - b));
    }
    return out;
}

bool TtsPiper::synthesize(const std::string& text, const std::string& voice,
                          std::vector<int16_t>& out_pcm, int& out_sample_rate) {
    out_pcm.clear();
    out_sample_rate = 22050;
    if (!ready_ || text.empty()) return false;

    std::string name = voice.empty() ? d_->default_voice : voice;
    auto model = d_->resolveModel(name);
    if (model.empty() && name != d_->default_voice) {
        name = d_->default_voice;
        model = d_->resolveModel(name);
    }
    if (model.empty()) {
        PM_WARN("audio.tts: voice '{}' not found under {}", name, d_->voices_dir.string());
        return false;
    }
    const auto cfg = d_->voices_dir / name / (name + ".onnx.json");
    out_sample_rate = d_->sampleRate(cfg);

    // Prefer persistent process path (also warms the watchdog).
    if (d_->ensureProc(name)) {
        if (d_->synthLine(text, out_pcm) && !out_pcm.empty())
            return true;
        // Process may have died — fall through to one-shot.
        d_->killProc();
    }
    return d_->synthOnce(text, name, out_pcm, out_sample_rate);
}

bool TtsPiper::synthesizeSentences(const std::string& text, const std::string& voice,
                                   std::vector<std::vector<int16_t>>& chunks,
                                   int& out_sample_rate) {
    chunks.clear();
    out_sample_rate = 22050;
    if (!ready_ || text.empty()) return false;

    const auto sentences = splitSentences(text);
    if (sentences.empty()) return false;

    std::string name = voice.empty() ? d_->default_voice : voice;
    auto model = d_->resolveModel(name);
    if (model.empty() && name != d_->default_voice) {
        name = d_->default_voice;
        model = d_->resolveModel(name);
    }
    if (model.empty()) return false;
    out_sample_rate = d_->sampleRate(d_->voices_dir / name / (name + ".onnx.json"));

    if (!d_->ensureProc(name)) {
        // One-shot whole text as a single chunk so tests still get audio.
        std::vector<int16_t> pcm;
        if (!d_->synthOnce(text, name, pcm, out_sample_rate) || pcm.empty())
            return false;
        chunks.push_back(std::move(pcm));
        return true;
    }

    for (const auto& s : sentences) {
        if (d_->cancel.load(std::memory_order_acquire)) return false;
        std::vector<int16_t> pcm;
        if (!d_->synthLine(s, pcm) || pcm.empty()) {
            // Restart once and retry the sentence.
            d_->killProc();
            if (!d_->ensureProc(name) || !d_->synthLine(s, pcm) || pcm.empty())
                continue;
        }
        chunks.push_back(std::move(pcm));
    }
    return !chunks.empty();
}

bool TtsPiper::speak(const std::string& text, const std::string& voice, bool append) {
    if (!ready_ || text.empty()) return false;

    d_->cancel.store(false, std::memory_order_release);
    d_->synth_active.store(true, std::memory_order_release);
    if (!append)
        d_->clearQueue();

    const auto sentences = splitSentences(text);
    if (sentences.empty()) {
        d_->synth_active.store(false, std::memory_order_release);
        return false;
    }

    std::string name = voice.empty() ? d_->default_voice : voice;
    auto model = d_->resolveModel(name);
    if (model.empty() && name != d_->default_voice) {
        name = d_->default_voice;
        model = d_->resolveModel(name);
    }
    if (model.empty()) {
        d_->synth_active.store(false, std::memory_order_release);
        return false;
    }
    const int sr = d_->sampleRate(d_->voices_dir / name / (name + ".onnx.json"));
    if (!d_->ensurePlayback(sr)) {
        d_->synth_active.store(false, std::memory_order_release);
        return false;
    }

    bool any = false;
    bool persistent_ok = d_->ensureProc(name);

    for (const auto& s : sentences) {
        if (d_->cancel.load(std::memory_order_acquire)) break;
        std::vector<int16_t> pcm;
        bool ok = false;
        if (persistent_ok) {
            ok = d_->synthLine(s, pcm);
            if (!ok) {
                // Watchdog: restart and retry once.
                d_->killProc();
                persistent_ok = d_->ensureProc(name);
                if (persistent_ok)
                    ok = d_->synthLine(s, pcm);
            }
        }
        if (!ok) {
            int tmp_sr = sr;
            ok = d_->synthOnce(s, name, pcm, tmp_sr);
        }
        if (!ok || pcm.empty()) continue;
        d_->enqueue(std::move(pcm));
        any = true;
    }

    d_->synth_active.store(false, std::memory_order_release);
    if (!any) return false;

    if (!append) {
        d_->waitUntilDrained();
        PM_DEBUG("audio.tts: spoke {} sentence chunk(s)", sentences.size());
    } else {
        PM_DEBUG("audio.tts: streamed {} sentence chunk(s)", sentences.size());
    }
    return !d_->cancel.load(std::memory_order_acquire);
}

void TtsPiper::endStream() {
    d_->waitUntilDrained();
}

bool TtsPiper::warmUp(const std::string& voice) {
    if (!ready_) return false;
    std::string name = voice.empty() ? d_->default_voice : voice;
    if (!d_->ensureProc(name)) return false;
    // Force voice load + first-byte path so the next user-visible line is warm.
    std::vector<int16_t> pcm;
    const bool ok = d_->synthLine("Ready.", pcm);
    if (!ok || pcm.empty()) {
        d_->killProc();
        if (!d_->ensureProc(name)) return false;
        return d_->synthLine("Ready.", pcm) && !pcm.empty();
    }
    PM_INFO("audio.tts: warmed voice '{}'", name);
    return true;
}

void TtsPiper::stop() {
    d_->cancel.store(true, std::memory_order_release);
    d_->clearQueue();
}

bool TtsPiper::isSpeaking() const {
    return d_->synth_active.load(std::memory_order_acquire) ||
           d_->play_active.load(std::memory_order_acquire) ||
           !const_cast<Impl*>(d_.get())->queueEmpty();
}

} // namespace polymath::audio
