#include "tts_piper.h"
#include "logging.h"

#include <QProcess>
#include <QProcessEnvironment>
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
#include <unordered_set>

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
// utterance complete (engine blocks on the next stdin line when done).
// First-byte budget is larger: cold start loads ONNX / espeak data.
// Kokoro first sentence can take a few seconds on CPU after model load.
constexpr int kUtteranceIdleMs    = 350;
constexpr int kFirstByteMs        = 45000;
constexpr int kUtteranceMaxMs     = 120000;
// Kokoro cold-load can take 15–45 s on a busy laptop; fail closed so we
// never claim "ready" and then hang the first speak with empty PCM.
constexpr int kKokoroBootMs       = 90000;

bool isSentenceEnd(char c) {
    return c == '.' || c == '!' || c == '?' || c == ';' || c == '\n';
}

std::string toLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Known abbreviation words (without the trailing period) that must not be
// treated as sentence boundaries when followed by a mid-sentence period.
// Single-letter tokens (initials, "U.S.", "e.g.", "i.e.") are handled
// separately in isAbbreviationPeriod() so they don't need listing here.
const std::unordered_set<std::string>& abbreviationWords() {
    static const std::unordered_set<std::string> words = {
        "dr", "mr", "mrs", "ms", "prof", "sr", "jr", "st", "vs", "etc",
        "approx", "inc", "ltd", "co", "ave", "blvd", "fig", "no", "vol",
        "gen", "rev", "col", "capt", "lt", "sgt", "cmdr", "gov", "rep",
        "sen", "pres", "dept", "univ", "ph", "esq", "hon", "assoc",
        "jan", "feb", "mar", "apr", "jun", "jul", "aug", "sep", "sept",
        "oct", "nov", "dec", "mon", "tue", "wed", "thu", "fri", "sat", "sun",
    };
    return words;
}

// The word immediately before `dot_index` in `text` (not including the dot
// itself), i.e. the contiguous run of non-whitespace characters ending right
// before the period.
std::string wordBeforeDot(const std::string& text, size_t dot_index) {
    if (dot_index == 0) return "";
    size_t end = dot_index;   // exclusive
    size_t start = end;
    while (start > 0 && !std::isspace(static_cast<unsigned char>(text[start - 1])))
        --start;
    return text.substr(start, end - start);
}

// True when the '.' at `i` in `text` should NOT be treated as a sentence
// boundary: decimal numbers (3.14), single-letter initials/acronyms (U.S.,
// e.g., i.e.), and a curated abbreviation list (Dr., Mr., etc.).
bool isAbbreviationPeriod(const std::string& text, size_t i) {
    const std::string word = wordBeforeDot(text, i);
    if (word.empty()) return false;

    // Decimal number: digit before AND digit after the period.
    if (std::isdigit(static_cast<unsigned char>(word.back())) &&
        i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])))
        return true;

    // Single-letter word ("U", "S", "e", "i", initials in "J. Smith") — very
    // likely an initial or part of an acronym like U.S. / e.g. / i.e.
    if (word.size() == 1 && std::isalpha(static_cast<unsigned char>(word[0])))
        return true;

    return abbreviationWords().count(toLowerAscii(word)) > 0;
}

// Merge consecutive short sentence fragments (<40 chars) forward into the
// next one so choppy one-word/short-clause utterances aren't each synthesized
// (and paused between) in isolation — smoother prosody for TTS. The last
// fragment always flushes on its own even if still short.
constexpr size_t kMergeShortFragmentChars = 40;

std::vector<std::string> mergeShortFragments(std::vector<std::string> sentences) {
    if (sentences.size() <= 1) return sentences;
    std::vector<std::string> merged;
    std::string pending;
    for (size_t i = 0; i < sentences.size(); ++i) {
        pending = pending.empty() ? sentences[i] : (pending + " " + sentences[i]);
        const bool isLast = (i + 1 == sentences.size());
        if (pending.size() < kMergeShortFragmentChars && !isLast)
            continue;   // fold the next sentence in too
        merged.push_back(pending);
        pending.clear();
    }
    if (!pending.empty()) merged.push_back(pending);
    return merged;
}

// Normalizes a caller-supplied default voice into the id TtsPiper::Impl
// should treat as the fallback for the given engine. For Kokoro, bare
// af_*/am_*/bf_*/bm_* (and already-prefixed "kokoro-*") ids are kept as-is;
// anything unrecognised falls back to the warm af_heart voice rather than
// the flatter af_sky. Piper ids pass through untouched (Piper voices are
// directory names, not a fixed enum).
std::string normalizeDefaultVoice(const std::string& engine, const std::string& voice) {
    if (engine != "kokoro") return voice;
    std::string dv = voice;
    if (dv.rfind("kokoro-", 0) == 0) return dv;
    if (dv.rfind("af_", 0) == 0 || dv.rfind("am_", 0) == 0 ||
        dv.rfind("bf_", 0) == 0 || dv.rfind("bm_", 0) == 0)
        return "kokoro-" + dv;
    if (dv.empty()) return "kokoro-af_heart";
    // Unrecognised shape (legacy Piper id or typo) — warm neutral default;
    // mapVoice() still tries a legacy-name heuristic per-utterance.
    return "kokoro-af_heart";
}

} // namespace

struct TtsPiper::Impl {
    std::filesystem::path voices_dir;
    std::string           default_voice;
    std::filesystem::path piper_exe;
    std::string           output_device;

    // Engine: "piper" (default) or "kokoro" (neural, preferred when present).
    std::string engine = "piper";
    int         engine_sr = 22050;  // Kokoro = 24000; Piper from voice config
    // Preference consulted by init(): auto|kokoro|piper (config tts.engine).
    std::string engine_pref = "auto";
    // Kokoro speed multiplier + output gain (config tts.speed / tts.volume).
    // Guarded by proc_mu / play_mu respectively (see setSpeed()/setVolume()).
    double      speed  = 1.0;
    double      volume = 1.0;

    // Persistent piper/kokoro process (one voice at a time).
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
        if (engine == "kokoro") return engine_sr > 0 ? engine_sr : 24000;
        try {
            std::ifstream in(cfg);
            auto j = nlohmann::json::parse(in);
            if (j.contains("audio") && j["audio"].contains("sample_rate"))
                return j["audio"]["sample_rate"].get<int>();
        } catch (...) {}
        return engine_sr > 0 ? engine_sr : 22050;
    }

    // All Kokoro voices shipped in the installed voices-v1.0.bin that D4
    // targets (English af_*/am_*/bf_*/bm_* — verified against the on-disk
    // file 2026-07-10; the bin also carries other-locale voices we don't
    // surface here). Used to validate ids and log a clear fallback reason.
    static const std::unordered_set<std::string>& shippedKokoroVoices() {
        static const std::unordered_set<std::string> v = {
            "af_alloy", "af_aoede", "af_bella", "af_heart", "af_jessica",
            "af_kore", "af_nicole", "af_nova", "af_river", "af_sarah", "af_sky",
            "am_adam", "am_echo", "am_eric", "am_fenrir", "am_liam",
            "am_michael", "am_onyx", "am_puck", "am_santa",
            "bf_alice", "bf_emma", "bf_isabella", "bf_lily",
            "bm_daniel", "bm_fable", "bm_george", "bm_lewis",
        };
        return v;
    }

    // Map personality/legacy Piper voice ids onto a Kokoro voice when using
    // the neural engine so existing personas still speak. Persona voice (the
    // `voice` arg) overrides the configured global default when non-empty.
    std::string mapVoice(const std::string& voice) const {
        if (engine != "kokoro") return voice.empty() ? default_voice : voice;

        const std::string requested = voice.empty() ? default_voice : voice;
        std::string bare = requested;
        if (bare.rfind("kokoro-", 0) == 0) bare = bare.substr(7);

        // Recognised shipped voice — use directly.
        if (shippedKokoroVoices().count(bare))
            return "kokoro-" + bare;

        // af_*/am_*/bf_*/bm_*-shaped but not in the known list (e.g. a newer
        // voices.bin drop) — trust it; the worker reports a clear error if
        // it truly doesn't exist, and that's better than silently ignoring
        // an intentional persona/voice choice.
        if (bare.rfind("af_", 0) == 0 || bare.rfind("am_", 0) == 0 ||
            bare.rfind("bf_", 0) == 0 || bare.rfind("bm_", 0) == 0) {
            PM_WARN("audio.tts: voice '{}' not in the known shipped list, trying anyway", bare);
            return "kokoro-" + bare;
        }

        // Legacy Piper ids -> best-effort Kokoro pick by name cue.
        std::string lower = bare;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        static const std::pair<const char*, const char*> legacy[] = {
            {"alan", "am_adam"}, {"ryan", "am_adam"}, {"joe", "am_fenrir"},
            {"northern_english_male", "am_michael"}, {"amy", "af_heart"},
            {"lessac", "af_heart"}, {"libritts", "af_sarah"},
            {"jenny", "bf_emma"}, {"kathleen", "bf_alice"},
        };
        for (const auto& [needle, mapped] : legacy) {
            if (lower.find(needle) != std::string::npos) {
                PM_INFO("audio.tts: mapped legacy voice '{}' -> Kokoro '{}'", requested, mapped);
                return std::string("kokoro-") + mapped;
            }
        }

        // Completely unrecognised id — fall back to the configured default
        // and log why, so a typo'd persona voice doesn't silently go quiet.
        const std::string fallback = default_voice.empty() ? "kokoro-af_heart" : default_voice;
        PM_WARN("audio.tts: unrecognised voice '{}', falling back to '{}'", requested, fallback);
        return fallback;
    }

    std::filesystem::path resolveModel(const std::string& name) const {
        // Kokoro: voices live in voices.bin; stub dir is enough.
        if (engine == "kokoro") {
            auto model = voices_dir / name / (name + ".onnx");
            if (std::filesystem::exists(model)) return model;
            // Non-empty sentinel so callers treat the voice as resolved.
            return voices_dir / (name.empty() ? default_voice : name);
        }
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

    // Build argv for a Kokoro worker process (shared by ensureProc / synthOnce).
    bool configureKokoroProcess(QProcess& p, const std::string& kvoice) {
        const auto engDir = piper_exe.parent_path();
        const auto py     = engDir / "venv" / "Scripts" / "python.exe";
        const auto worker = engDir / "kokoro_worker.py";
        const auto modelp = engDir / "kokoro-v1.0.onnx";
        const auto voices = engDir / "voices-v1.0.bin";

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
        env.insert(QStringLiteral("KOKORO_VOICE"), QString::fromStdString(kvoice));
        env.insert(QStringLiteral("KOKORO_SPEED"), QString::number(speed, 'f', 3));
        p.setProcessEnvironment(env);
        p.setWorkingDirectory(QString::fromStdString(engDir.string()));
        p.setProcessChannelMode(QProcess::SeparateChannels);

        if (std::filesystem::exists(py) && std::filesystem::exists(worker) &&
            std::filesystem::exists(modelp) && std::filesystem::exists(voices)) {
            // -u: force unbuffered stdio so PCM flushes immediately under QProcess.
            p.setProgram(QString::fromStdString(py.string()));
            p.setArguments(QStringList{}
                << QStringLiteral("-u")
                << QString::fromStdString(worker.string())
                << QStringLiteral("--model")  << QString::fromStdString(modelp.string())
                << QStringLiteral("--voices") << QString::fromStdString(voices.string())
                << QStringLiteral("--voice")  << QString::fromStdString(kvoice)
                << QStringLiteral("--speed")  << QString::number(speed, 'f', 3)
                << QStringLiteral("--sample-rate") << QStringLiteral("24000"));
            return true;
        }
        if (std::filesystem::exists(piper_exe)) {
            // Fall back: cmd.exe /c kokoro_worker.cmd
            p.setProgram(QStringLiteral("cmd.exe"));
            p.setArguments(QStringList{}
                << QStringLiteral("/c")
                << QString::fromStdString(piper_exe.string())
                << QStringLiteral("--voice") << QString::fromStdString(kvoice));
            return true;
        }
        PM_ERROR("audio.tts: Kokoro worker/model missing under {}", engDir.string());
        return false;
    }

    // Block until kokoro_worker prints "ready" on stderr (or fail).
    bool waitKokoroReady(QProcess& p, QByteArray* err_out = nullptr) {
        QElapsedTimer boot;
        boot.start();
        QByteArray err;
        while (boot.elapsed() < kKokoroBootMs && p.state() == QProcess::Running) {
            // Ready is on stderr; poll both channels so stdout never fills up.
            p.waitForReadyRead(100);
            err += p.readAllStandardError();
            // Discard any unexpected stdout noise during boot.
            (void)p.readAllStandardOutput();
            if (err.contains("kokoro_worker: ready")) {
                if (err_out) *err_out = err;
                return true;
            }
            if (err.contains("failed to load") || err.contains("not installed") ||
                err.contains("model not found") || err.contains("voices not found")) {
                break;
            }
        }
        err += p.readAllStandardError();
        if (err_out) *err_out = err;
        return err.contains("kokoro_worker: ready");
    }

    // Ensure a live TTS process for `voice`. Restarts on voice change / crash.
    bool ensureProc(const std::string& voice) {
        std::lock_guard<std::mutex> lock(proc_mu);
        if (proc && proc->state() == QProcess::Running && proc_voice == voice)
            return true;

        killProcUnlocked();

        std::string name = mapVoice(voice);
        auto model = resolveModel(name);
        if (model.empty() && engine != "kokoro") {
            if (name != default_voice) {
                name = default_voice;
                model = resolveModel(name);
            }
        }
        if (model.empty() && engine != "kokoro") {
            PM_WARN("audio.tts: voice '{}' not found under {}", name, voices_dir.string());
            return false;
        }

        proc = std::make_unique<QProcess>();
        proc->setWorkingDirectory(
            QString::fromStdString(piper_exe.parent_path().string()));
        proc->setProcessChannelMode(QProcess::SeparateChannels);

        if (engine == "kokoro") {
            std::string kvoice = name;
            if (kvoice.rfind("kokoro-", 0) == 0)
                kvoice = kvoice.substr(7);
            if (kvoice.empty()) kvoice = "af_heart";

            if (!configureKokoroProcess(*proc, kvoice)) {
                proc.reset();
                return false;
            }
        } else {
            const auto cfg = voices_dir / name / (name + ".onnx.json");
            proc->setProgram(QString::fromStdString(piper_exe.string()));
            proc->setArguments(QStringList{}
                << "--model"  << QString::fromStdString(model.string())
                << "--config" << QString::fromStdString(cfg.string())
                << "--output_raw");
        }
        proc->start();
        if (!proc->waitForStarted(15000)) {
            PM_ERROR("audio.tts: failed to start {} engine", engine);
            proc.reset();
            return false;
        }
        // Kokoro: require the ready banner. Returning true without it made the
        // first speak() race a still-loading model and produce zero PCM.
        if (engine == "kokoro") {
            QByteArray err;
            if (!waitKokoroReady(*proc, &err)) {
                PM_ERROR("audio.tts: kokoro failed to become ready ({})",
                         QString::fromUtf8(err.left(400)).toStdString());
                killProcUnlocked();
                return false;
            }
        }
        proc_voice = name;
        PM_INFO("audio.tts: persistent {} started (voice '{}')", engine, name);
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
            const QByteArray err = proc->readAllStandardError().left(400);
            PM_ERROR("audio.tts: {} produced no audio ({})",
                     engine, QString::fromUtf8(err).toStdString());
            return false;
        }
        // Near-silence (e.g. Kokoro error path writes 240 zero samples) is not
        // usable speech — treat as failure so the caller can restart/retry.
        const size_t n = static_cast<size_t>(raw.size()) / sizeof(int16_t);
        if (n < 800) {  // < ~33 ms @ 24 kHz
            PM_ERROR("audio.tts: {} produced only {} samples (too short)",
                     engine, n);
            return false;
        }
        // Piper may emit an odd trailing byte; drop incomplete sample.
        out.resize(n);
        std::memcpy(out.data(), raw.constData(), n * sizeof(int16_t));
        return !out.empty();
    }

    // One-shot fallback (non-persistent) used when the persistent path fails.
    bool synthOnce(const std::string& text, const std::string& voice,
                   std::vector<int16_t>& out, int& sr) {
        out.clear();
        std::string name = mapVoice(voice);
        auto model = resolveModel(name);
        if (model.empty() && engine != "kokoro" && name != default_voice) {
            name = default_voice;
            model = resolveModel(name);
        }
        if (model.empty() && engine != "kokoro") return false;

        if (engine == "kokoro") {
            sr = engine_sr > 0 ? engine_sr : 24000;
            std::string kvoice = name;
            if (kvoice.rfind("kokoro-", 0) == 0) kvoice = kvoice.substr(7);
            if (kvoice.empty()) kvoice = "af_heart";

            QProcess p;
            if (!configureKokoroProcess(p, kvoice)) return false;
            p.start();
            if (!p.waitForStarted(15000)) return false;
            QByteArray bootErr;
            if (!waitKokoroReady(p, &bootErr)) {
                PM_ERROR("audio.tts: kokoro one-shot boot failed ({})",
                         QString::fromUtf8(bootErr.left(300)).toStdString());
                p.kill();
                p.waitForFinished(2000);
                return false;
            }
            QByteArray payload = QByteArray::fromStdString(text);
            if (!payload.endsWith('\n')) payload.append('\n');
            p.write(payload);
            p.waitForBytesWritten(2000);
            p.closeWriteChannel();

            QByteArray raw;
            QElapsedTimer timer;
            timer.start();
            qint64 last_data_ms = -1;
            while (timer.elapsed() < kUtteranceMaxMs) {
                if (p.state() != QProcess::Running) {
                    raw += p.readAllStandardOutput();
                    break;
                }
                if (p.waitForReadyRead(40)) {
                    raw += p.readAllStandardOutput();
                    last_data_ms = timer.elapsed();
                } else {
                    const QByteArray more = p.readAllStandardOutput();
                    if (!more.isEmpty()) {
                        raw += more;
                        last_data_ms = timer.elapsed();
                    } else if (last_data_ms >= 0 &&
                               (timer.elapsed() - last_data_ms) >= kUtteranceIdleMs) {
                        break;
                    } else if (last_data_ms < 0 && timer.elapsed() > kFirstByteMs) {
                        break;
                    }
                }
                (void)p.readAllStandardError();
            }
            p.kill();
            p.waitForFinished(2000);
            const size_t n = static_cast<size_t>(raw.size()) / sizeof(int16_t);
            if (n < 800) {
                PM_ERROR("audio.tts: kokoro one-shot produced no usable audio");
                return false;
            }
            out.resize(n);
            std::memcpy(out.data(), raw.constData(), n * sizeof(int16_t));
            return true;
        }

        // Piper one-shot.
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
        if (std::abs(volume - 1.0) > 1e-3) {
            for (auto& s : pcm) {
                double v = static_cast<double>(s) * volume;
                v = std::clamp(v, -32768.0, 32767.0);
                s = static_cast<int16_t>(v);
            }
        }
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
    d_->output_device = output_device;
    const auto models_root = voices_dir.parent_path();

    // Prefer Kokoro neural TTS when the engine was set up (setup-kokoro.ps1).
    // It runs on CPU so it does not compete with the LLM for VRAM, and quality
    // is far above Piper medium voices. `tts.engine` (auto|kokoro|piper) lets
    // the user force one or the other; "auto" keeps the original
    // file-presence autodetect behaviour.
    const auto kokoro_cmd     = models_root / "kokoro-engine" / "kokoro_worker.cmd";
    const auto kokoro_onnx    = models_root / "kokoro-engine" / "kokoro-v1.0.onnx";
    const auto kokoro_voices  = models_root / "kokoro";
    const bool kokoro_present = std::filesystem::exists(kokoro_cmd) &&
                                 std::filesystem::exists(kokoro_onnx);

    const std::string pref = d_->engine_pref.empty() ? "auto" : d_->engine_pref;
    if (pref == "kokoro" && !kokoro_present) {
        PM_WARN("audio.tts: tts.engine=kokoro requested but the Kokoro worker/model "
                "are missing under {}; falling back to piper",
                (models_root / "kokoro-engine").string());
    }
    const bool use_kokoro = (pref != "piper") && kokoro_present;

    if (use_kokoro) {
        d_->engine      = "kokoro";
        d_->engine_sr   = 24000;
        d_->piper_exe   = kokoro_cmd;
        d_->voices_dir  = std::filesystem::exists(kokoro_voices)
                              ? kokoro_voices : voices_dir;
        d_->default_voice = normalizeDefaultVoice(d_->engine, default_voice);
        ready_ = true;
        PM_INFO("audio.tts: Kokoro neural engine at {} (default voice '{}', {} Hz, "
                "engine pref '{}')",
                d_->piper_exe.string(), d_->default_voice, d_->engine_sr, pref);
        return ready_;
    }

    // Fallback / forced: Piper (still neural, lower quality).
    d_->engine        = "piper";
    d_->engine_sr     = 22050;
    d_->voices_dir    = voices_dir;
    d_->default_voice = default_voice;
    d_->piper_exe = models_root / "piper-engine" / "piper.exe";
    ready_ = std::filesystem::exists(d_->piper_exe);
    if (ready_)
        PM_INFO("audio.tts: Piper engine at {} (default voice '{}')",
                d_->piper_exe.string(), default_voice);
    else
        PM_WARN("audio.tts: no TTS engine found (run scripts/setup-kokoro.ps1 "
                "or scripts/fetch-models.ps1)");
    return ready_;
}

void TtsPiper::setEnginePreference(const std::string& pref) {
    d_->engine_pref = pref.empty() ? "auto" : pref;
}

void TtsPiper::setDefaultVoice(const std::string& voice) {
    if (voice.empty()) return;
    d_->default_voice = normalizeDefaultVoice(d_->engine, voice);
}

void TtsPiper::setSpeed(double speed) {
    speed = std::clamp(speed, 0.5, 2.0);   // wide safety clamp; UI clamps 0.8-1.3
    std::lock_guard<std::mutex> lock(d_->proc_mu);
    if (std::abs(d_->speed - speed) < 1e-6) return;
    d_->speed = speed;
    if (d_->engine == "kokoro" && d_->proc && d_->proc->state() == QProcess::Running) {
        const QByteArray line = QByteArray::fromStdString(
            "!speed=" + std::to_string(speed) + "\n");
        d_->proc->write(line);
        d_->proc->waitForBytesWritten(500);
    }
}

void TtsPiper::setVolume(double volume) {
    volume = std::clamp(volume, 0.0, 2.0);
    std::lock_guard<std::mutex> lock(d_->play_mu);
    d_->volume = volume;
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
        bool boundary = isSentenceEnd(c);
        // Abbreviation/decimal guard only applies to '.': Dr. / e.g. / i.e. /
        // U.S. / 3.14 should not split; '!'/'?'/';'/'\n' always end a chunk.
        if (boundary && c == '.' && isAbbreviationPeriod(text, i))
            boundary = false;
        if (boundary) {
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

    // A fresh top-level synth is a new operation; clear any lingering cancel
    // from a prior stop()/barge-in so synthLine doesn't bail (mirrors speak()).
    d_->cancel.store(false, std::memory_order_release);

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

    // Fresh operation — clear any lingering cancel from a prior stop() so the
    // per-sentence cancel check below doesn't short-circuit (mirrors speak()).
    d_->cancel.store(false, std::memory_order_release);

    const auto sentences = mergeShortFragments(splitSentences(text));
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

    const auto sentences = mergeShortFragments(splitSentences(text));
    if (sentences.empty()) {
        d_->synth_active.store(false, std::memory_order_release);
        return false;
    }

    std::string name = d_->mapVoice(voice);
    auto model = d_->resolveModel(name);
    if (model.empty() && d_->engine != "kokoro") {
        if (name != d_->default_voice) {
            name = d_->default_voice;
            model = d_->resolveModel(name);
        }
    }
    if (model.empty() && d_->engine != "kokoro") {
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
    std::string name = d_->mapVoice(voice);
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
