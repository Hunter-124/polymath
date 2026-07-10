#include "audio_service.h"
#include "database.h"
#include "event_bus.h"
#include "config.h"
#include "paths.h"
#include "logging.h"

#include "audio_common.h"
#include "capture.h"
#include "wakeword.h"
#include "vad.h"
#include "asr_whisper.h"
#include "tts_piper.h"

#include <QTimer>
#include <QThread>
#include <QString>
#include <QVector>
#include <QMetaObject>
#include <QMetaType>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

// AudioService pimpl: pump thread does ONLY drain→VAD→gated oWW→segments.
// ASR (whisper_full) and TTS (piper+playback) each run on dedicated QThreads.

namespace polymath {

using namespace polymath::audio;

namespace {
enum class Listen { Idle, Wake, Command, Ambient };

constexpr int kMaxCommandSec = 15;
constexpr int kMaxAmbientSec = 30;

// Barge-in: raised thresholds while TTS is playing (04 §3.5).
constexpr float kBaseVadThresh   = 0.5f;
constexpr float kBaseWakeThresh  = 0.5f;
constexpr float kBargeInDelta    = 0.1f;
constexpr float kBargeInMinRms   = 0.015f;  // energy gate vs playback loudness

using ClockSteady = std::chrono::steady_clock;
}

// ---------------------------------------------------------------------------
// AsrWorker — lives on its own QThread; runs whisper_full off the pump.
// ---------------------------------------------------------------------------
class AsrWorker : public QObject {
    Q_OBJECT
public:
    AsrWorker(AsrWhisper& command, AsrWhisper& ambient,
              std::filesystem::path cmd_path, std::filesystem::path amb_path,
              QObject* parent = nullptr)
        : QObject(parent)
        , command_(command)
        , ambient_(ambient)
        , cmd_path_(std::move(cmd_path))
        , amb_path_(std::move(amb_path)) {}

public slots:
    // Preload base.en on wake/PTT so load overlaps the listening earcon.
    void preloadCommand() {
        if (!cmd_path_.empty())
            command_.load(cmd_path_, AsrWhisper::Mode::Command);
    }
    void preloadAmbient() {
        if (!amb_path_.empty())
            ambient_.load(amb_path_, AsrWhisper::Mode::Ambient);
    }
    void unloadIdle() {
        if (command_.isLoaded()) command_.unload();
        if (ambient_.isLoaded()) ambient_.unload();
    }
    void unloadAmbient() {
        if (ambient_.isLoaded()) ambient_.unload();
    }
    void process(const QVector<float>& pcm, bool ambient) {
        AsrWhisper& asr = ambient ? ambient_ : command_;
        if (!asr.isLoaded()) {
            const auto& path = ambient ? amb_path_ : cmd_path_;
            if (path.empty() || !asr.load(path, ambient ? AsrWhisper::Mode::Ambient
                                                        : AsrWhisper::Mode::Command)) {
                emit finished(QString{}, 0.0f, ambient);
                return;
            }
        }
        std::vector<float> samples(pcm.begin(), pcm.end());
        float conf = 0.0f;
        const std::string text = asr.transcribe(samples, &conf);
        emit finished(QString::fromStdString(text), conf, ambient);
    }

signals:
    void finished(const QString& text, float conf, bool ambient);

private:
    AsrWhisper& command_;
    AsrWhisper& ambient_;
    std::filesystem::path cmd_path_;
    std::filesystem::path amb_path_;
};

// ---------------------------------------------------------------------------
// TtsWorker — lives on its own QThread; owns the blocking speak() call.
// ---------------------------------------------------------------------------
class TtsWorker : public QObject {
    Q_OBJECT
public:
    explicit TtsWorker(TtsPiper& tts, QObject* parent = nullptr)
        : QObject(parent), tts_(tts) {}

public slots:
    void process(const QString& text, const QString& voice) {
        tts_.speak(text.toStdString(), voice.toStdString());
        emit finished();
    }
    void cancel() { tts_.stop(); }

signals:
    void finished();

private:
    TtsPiper& tts_;
};

// ---------------------------------------------------------------------------
// AudioService::Impl
// ---------------------------------------------------------------------------
struct AudioService::Impl {
    explicit Impl(Database& db) : db(db) {}

    Database& db;

    Capture     capture;
    WakeWord    wake;
    Vad         vad;
    AsrWhisper  asr_command;
    AsrWhisper  asr_ambient;
    TtsPiper    tts;

    // Model paths (resolved at start; used for lazy load).
    std::filesystem::path cmd_model_path;
    std::filesystem::path amb_model_path;

    QTimer* timer = nullptr;

    // Dedicated workers (04 §3.3 / §3.4).
    QThread*   asr_thread  = nullptr;
    AsrWorker* asr_worker  = nullptr;
    QThread*   tts_thread  = nullptr;
    TtsWorker* tts_worker  = nullptr;

    bool mic_enabled     = true;
    bool ambient_enabled = false;
    int  asr_idle_unload_s = 90;
    std::string input_device;
    std::string output_device;

    std::atomic<bool> ptt_down{false};
    std::atomic<bool> speaking{false};     // TTS in flight (barge-in mode)
    std::atomic<bool> asr_busy{false};     // whisper_full running

    Listen state       = Listen::Idle;
    bool   ptt_segment = false;

    // Scratch / state buffers — no hot erase of large vectors.
    std::vector<float> frame;          // 1280-sample drain
    SampleRing         preroll;        // rolling pre-VAD context
    std::vector<float> segment;        // active speech segment
    std::vector<float> wake_speech;    // speech buffer while waiting for oWW
    float              vad_acc[kVadWindow]{};
    int                vad_fill = 0;

    // VAD-gated oWW hangover (ms remaining after SpeechEnd).
    int  oww_hangover_ms     = 0;
    bool oww_feed_preroll    = false;  // dump preroll into oWW on next speech start
    bool vad_in_speech       = false;

    int  command_timeout_frames = 0;

    // ASR residency tracking (04 §3.2).
    ClockSteady::time_point last_asr_activity{};
    bool asr_activity_valid = false;

    // --- helpers ---
    void loadModels();
    void applyCaptureState();
    void pump(AudioService* self);
    void feedFrame(AudioService* self, const float* data, int n);
    void runVadWindows(AudioService* self, const float* data, int n,
                       bool* speech_start, bool* speech_end);
    void handleWake(AudioService* self);
    void enterCommand(AudioService* self, bool from_ptt);
    void finishSegment(AudioService* self, bool ambient);
    void ensureCommandAsr();
    void ensureAmbientAsr();
    void maybeUnloadAsr();
    void touchAsrActivity();
    void applyBargeInThresholds(bool on);
    void stopTts(AudioService* self);
    void publishNotice(const char* level, const char* source, const char* msg);
};

void AudioService::Impl::publishNotice(const char* level, const char* source,
                                       const char* msg) {
    EventBus::instance().publishNotice({
        QString::fromUtf8(level),
        QString::fromUtf8(source),
        QString::fromUtf8(msg)
    });
}

void AudioService::Impl::loadModels() {
    const auto models = Paths::instance().models();
    Config cfg(db);

    const std::string phrase = cfg.getStr(keys::WakeWord, "hey_jarvis");
    wake.load(models / "wakeword", phrase);
    wake.setNoticeFn([this](const char* l, const char* s, const char* m) {
        publishNotice(l, s, m);
    });
    wake.setThreshold(kBaseWakeThresh);

    vad.load(models / "vad" / "silero_vad.onnx");
    vad.setNoticeFn([this](const char* l, const char* s, const char* m) {
        publishNotice(l, s, m);
    });
    vad.setThreshold(kBaseVadThresh);

    // Paths only — do NOT load whisper eagerly (04 §3.2).
    cmd_model_path = models / "whisper" / "ggml-base.en.bin";
    amb_model_path = models / "whisper" / "ggml-tiny.en.bin";
    asr_command.setThreads(4);
    asr_ambient.setThreads(2);

    // Ambient ASR is loaded lazily only when ambient_transcription is on and
    // a segment actually finishes; we may preload here if already enabled to
    // avoid first-segment latency, but never load command ASR at start.
    if (ambient_enabled) {
        // Spec: "tiny.en loads only when ambient_transcription is enabled (lazy)"
        // Keep fully lazy — load on first ambient segment.
    }

    tts.init(models / "piper", "en_US-amy-medium", output_device);

    capture.init(input_device);

    preroll.reset(static_cast<size_t>(kSampleRate) * kPreRollMs / 1000);

    PM_INFO("AudioService models: wake={} vad={} asr=lazy tts={}",
            wake.ready(), vad.ready(), tts.ready());
}

void AudioService::Impl::touchAsrActivity() {
    last_asr_activity = ClockSteady::now();
    asr_activity_valid = true;
}

void AudioService::Impl::ensureCommandAsr() {
    // Kick load on the ASR thread so the pump never races whisper context.
    touchAsrActivity();
    if (asr_worker) {
        QMetaObject::invokeMethod(asr_worker, "preloadCommand", Qt::QueuedConnection);
    } else if (!cmd_model_path.empty()) {
        asr_command.load(cmd_model_path, AsrWhisper::Mode::Command);
    }
}

void AudioService::Impl::ensureAmbientAsr() {
    if (!ambient_enabled) return;
    touchAsrActivity();
    if (asr_worker) {
        QMetaObject::invokeMethod(asr_worker, "preloadAmbient", Qt::QueuedConnection);
    } else if (!amb_model_path.empty()) {
        asr_ambient.load(amb_model_path, AsrWhisper::Mode::Ambient);
    }
}

void AudioService::Impl::maybeUnloadAsr() {
    if (!asr_activity_valid) return;
    if (asr_busy.load(std::memory_order_acquire)) return;
    if (state == Listen::Command) return;   // interaction active
    if (speaking.load(std::memory_order_acquire)) return;

    const auto idle = std::chrono::duration_cast<std::chrono::seconds>(
        ClockSteady::now() - last_asr_activity).count();
    if (idle < asr_idle_unload_s) return;

    PM_INFO("audio.asr: idle-unload after {}s (threshold {}s)", idle, asr_idle_unload_s);
    if (asr_worker) {
        QMetaObject::invokeMethod(asr_worker, "unloadIdle", Qt::QueuedConnection);
    } else {
        asr_command.unload();
        asr_ambient.unload();
    }
    asr_activity_valid = false;
}

void AudioService::Impl::applyBargeInThresholds(bool on) {
    if (on) {
        vad.setThreshold(kBaseVadThresh + kBargeInDelta);
        wake.setThreshold(kBaseWakeThresh + kBargeInDelta);
    } else {
        vad.setThreshold(kBaseVadThresh);
        wake.setThreshold(kBaseWakeThresh);
    }
}

void AudioService::Impl::stopTts(AudioService* self) {
    tts.stop();
    if (tts_worker)
        QMetaObject::invokeMethod(tts_worker, "cancel", Qt::QueuedConnection);
    speaking.store(false, std::memory_order_release);
    applyBargeInThresholds(false);
    (void)self;
}

void AudioService::Impl::applyCaptureState() {
    Config cfg(db);
    // Honour live device settings (04 §3.6).
    const std::string in_dev  = cfg.getStr(keys::AudioInputDevice, "");
    const std::string out_dev = cfg.getStr(keys::AudioOutputDevice, "");
    if (in_dev != input_device) {
        input_device = in_dev;
        const bool was = capture.isRunning();
        capture.reinit(input_device);
        if (was) capture.start();
    }
    if (out_dev != output_device) {
        output_device = out_dev;
        tts.setOutputDevice(output_device);
    }
    asr_idle_unload_s = cfg.getInt(keys::AudioAsrIdleUnloadS, 90);
    if (asr_idle_unload_s < 5) asr_idle_unload_s = 5;

    const bool want = mic_enabled;
    if (want && !capture.isRunning()) {
        if (capture.start()) {
            wake.reset();
            vad.reset();
            preroll.clear();
            vad_fill = 0;
            oww_hangover_ms = 0;
            vad_in_speech = false;
        }
    } else if (!want && capture.isRunning()) {
        capture.stop();
        state = Listen::Idle;
        segment.clear();
        wake_speech.clear();
        preroll.clear();
        vad_fill = 0;
    }

    if (want) {
        if (state == Listen::Idle)
            state = ambient_enabled ? Listen::Ambient : Listen::Wake;
        else if (state == Listen::Wake && ambient_enabled)
            state = Listen::Ambient;
        else if (state == Listen::Ambient && !ambient_enabled)
            state = Listen::Wake;
    }
}

void AudioService::Impl::handleWake(AudioService* self) {
    // Barge-in: stop TTS first.
    if (speaking.load(std::memory_order_acquire))
        stopTts(self);

    emit self->wakeWordHeard();
    emit self->listeningStateChanged(true);
    EventBus::instance().publishWakeWord(
        {QString::fromStdString(wake.phrase()), to_unix(Clock::now())});

    ensureCommandAsr();   // lazy load base.en, overlapped with listening earcon

    state = Listen::Command;
    ptt_segment = false;
    command_timeout_frames = kSampleRate / kFrameSamples * 5;

    // Seed command segment with pre-roll + any speech accumulated while gated.
    segment.clear();
    preroll.appendTo(segment);
    if (!wake_speech.empty())
        segment.insert(segment.end(), wake_speech.begin(), wake_speech.end());
    wake_speech.clear();

    // Reset VAD recurrent state on mode transition (04 §3.1).
    vad.reset();
    vad_fill = 0;
    vad_in_speech = false;
    oww_hangover_ms = 0;
    oww_feed_preroll = false;
}

void AudioService::Impl::enterCommand(AudioService* self, bool from_ptt) {
    if (speaking.load(std::memory_order_acquire))
        stopTts(self);

    ensureCommandAsr();

    state = Listen::Command;
    ptt_segment = from_ptt;
    command_timeout_frames = kSampleRate / kFrameSamples * 5;
    segment.clear();
    preroll.appendTo(segment);
    wake_speech.clear();
    vad.reset();
    vad_fill = 0;
    vad_in_speech = false;
    oww_hangover_ms = 0;
    emit self->listeningStateChanged(true);
}

void AudioService::Impl::runVadWindows(AudioService* self, const float* data, int n,
                                       bool* speech_start, bool* speech_end) {
    (void)self;
    if (speech_start) *speech_start = false;
    if (speech_end)   *speech_end   = false;
    if (!vad.ready() || !data || n <= 0) return;

    int off = 0;
    while (off < n) {
        const int space = kVadWindow - vad_fill;
        const int take  = std::min(space, n - off);
        std::memcpy(vad_acc + vad_fill, data + off, static_cast<size_t>(take) * sizeof(float));
        vad_fill += take;
        off += take;

        if (vad_fill < kVadWindow) break;

        float prob = 0.0f;
        Vad::Event ev = vad.process(vad_acc, &prob);
        vad_fill = 0;

        if (ev == Vad::Event::SpeechStart) {
            vad_in_speech = true;
            oww_hangover_ms = 0;
            oww_feed_preroll = true;
            if (speech_start) *speech_start = true;
        } else if (ev == Vad::Event::SpeechEnd) {
            vad_in_speech = false;
            oww_hangover_ms = kOwwHangoverMs;
            if (speech_end) *speech_end = true;
        }
    }
}

void AudioService::Impl::feedFrame(AudioService* self, const float* data, int n) {
    // Maintain pre-roll so a segment / oWW dump includes the word onset.
    preroll.push(data, static_cast<size_t>(n));

    const bool is_speaking = speaking.load(std::memory_order_acquire);
    applyBargeInThresholds(is_speaking);

    bool speech_start = false, speech_end = false;
    runVadWindows(self, data, n, &speech_start, &speech_end);

    // Tick hangover while not in speech.
    if (!vad_in_speech && oww_hangover_ms > 0) {
        const int frame_ms = (n * 1000) / kSampleRate;
        oww_hangover_ms = std::max(0, oww_hangover_ms - frame_ms);
    }

    const bool oww_gate = vad_in_speech || oww_hangover_ms > 0;

    // ---- Barge-in / wake path (gated oWW) ---------------------------------
    // oWW runs only during VAD speech + hangover (04 §3.1). During TTS we still
    // run it (raised threshold + energy gate) so wake can stop playback.
    const bool want_oww =
        oww_gate &&
        (state == Listen::Wake || state == Listen::Ambient || is_speaking) &&
        wake.ready();

    if (want_oww) {
        bool energy_ok = true;
        if (is_speaking) {
            // Energy gate calibrated against playback loudness: ignore very
            // quiet frames (self-echo floors) and require real speech energy.
            energy_ok = frameRms(data, n) >= kBargeInMinRms;
        }

        if (energy_ok) {
            // On speech start, feed buffered pre-roll first so the phrase prefix
            // is not dropped (04 §3.1).
            if (oww_feed_preroll) {
                oww_feed_preroll = false;
                const auto pre = preroll.snapshot();
                for (size_t off = 0; off + static_cast<size_t>(kFrameSamples) <= pre.size();
                     off += static_cast<size_t>(kFrameSamples)) {
                    float sc = 0.0f;
                    if (wake.process(pre.data() + off, kFrameSamples, &sc)) {
                        handleWake(self);
                        return;
                    }
                }
            }

            float score = 0.0f;
            if (wake.process(data, n, &score)) {
                handleWake(self);
                return;
            }
        }

        // While waiting for wake, accumulate speech so a hit mid-phrase keeps
        // the command audio from speech onset.
        if (state == Listen::Wake && (vad_in_speech || oww_hangover_ms > 0))
            wake_speech.insert(wake_speech.end(), data, data + n);
    } else if (state == Listen::Wake && !oww_gate) {
        // Pure silence while armed: drop any stale wake_speech.
        if (!vad_in_speech && oww_hangover_ms == 0)
            wake_speech.clear();
    }

    // ---- Command / Ambient segmentation (same Silero instance) ------------
    if (state == Listen::Command || state == Listen::Ambient) {
        // During TTS we do not accumulate ambient segments (barge-in only).
        if (is_speaking && state == Listen::Ambient)
            return;

        const Listen seg_state = state;
        segment.insert(segment.end(), data, data + n);

        if (speech_end && state == Listen::Command && !ptt_segment) {
            finishSegment(self, false);
            return;
        }
        if (speech_end && state == Listen::Ambient) {
            finishSegment(self, true);
            return;
        }
        // Also finish command if VAD reports end via inSpeech transition that
        // happened inside runVadWindows (speech_end already handled). For
        // command opened mid-utterance, SpeechEnd may fire on later frames.
        if (state == Listen::Command && !ptt_segment && !vad.inSpeech() &&
            !vad_in_speech && oww_hangover_ms == 0 &&
            static_cast<int>(segment.size()) > kSampleRate / 5 &&
            speech_end) {
            finishSegment(self, false);
            return;
        }

        const int cap = (state == Listen::Command ? kMaxCommandSec : kMaxAmbientSec);
        if (static_cast<int>(segment.size()) > cap * kSampleRate) {
            finishSegment(self, state == Listen::Ambient);
            return;
        }

        if (state == Listen::Command && !ptt_segment && !vad.inSpeech() && !vad_in_speech) {
            command_timeout_frames -= (n / kFrameSamples) + 1;
            if (command_timeout_frames <= 0) {
                PM_DEBUG("audio: command timed out waiting for speech");
                segment.clear();
                state = ambient_enabled ? Listen::Ambient : Listen::Wake;
                emit self->listeningStateChanged(false);
            }
        }
        (void)seg_state;
    }
}

void AudioService::Impl::finishSegment(AudioService* self, bool ambient) {
    std::vector<float> pcm;
    pcm.swap(segment);
    vad.reset();
    vad_fill = 0;
    vad_in_speech = false;
    oww_hangover_ms = 0;
    wake_speech.clear();

    if (!ambient) {
        ptt_segment = false;
        emit self->listeningStateChanged(false);
        state = ambient_enabled ? Listen::Ambient : Listen::Wake;
    }

    // Ignore too-short blips (< 200 ms).
    if (pcm.size() < static_cast<size_t>(kSampleRate) / 5) return;

    if (ambient) {
        if (!ambient_enabled) return;
        if (amb_model_path.empty() || !std::filesystem::exists(amb_model_path))
            return;
        ensureAmbientAsr();
    } else {
        if (cmd_model_path.empty() || !std::filesystem::exists(cmd_model_path))
            return;
        ensureCommandAsr();
    }

    touchAsrActivity();
    asr_busy.store(true, std::memory_order_release);

    // Post to AsrWorker — pump must not block on whisper_full (04 §3.3).
    QVector<float> qpcm;
    qpcm.reserve(static_cast<int>(pcm.size()));
    for (float s : pcm) qpcm.push_back(s);

    if (asr_worker) {
        QMetaObject::invokeMethod(asr_worker, "process", Qt::QueuedConnection,
                                  Q_ARG(QVector<float>, qpcm),
                                  Q_ARG(bool, ambient));
    } else {
        // Fallback (tests / early start): run inline.
        AsrWhisper& asr = ambient ? asr_ambient : asr_command;
        float conf = 0.0f;
        const std::string text = asr.ready() ? asr.transcribe(pcm, &conf) : std::string{};
        asr_busy.store(false, std::memory_order_release);
        if (!text.empty()) {
            QMetaObject::invokeMethod(self, "onAsrFinished", Qt::QueuedConnection,
                                      Q_ARG(QString, QString::fromStdString(text)),
                                      Q_ARG(float, conf),
                                      Q_ARG(bool, ambient));
        }
    }
}

void AudioService::Impl::pump(AudioService* self) {
    maybeUnloadAsr();

    if (!capture.isRunning()) return;

    // Push-to-talk press: open the mic without a wake word; also preloads ASR.
    const bool ptt = ptt_down.load(std::memory_order_acquire);
    if (ptt && !ptt_segment) {
        enterCommand(self, true);
    }

    // Drain the capture ring in 1280-sample frames. Capture stays live during
    // TTS so barge-in can fire (04 §3.5) — we no longer clear the ring.
    frame.resize(static_cast<size_t>(kFrameSamples));
    while (capture.ring().available() >= static_cast<size_t>(kFrameSamples)) {
        size_t got = capture.ring().read(frame.data(), static_cast<size_t>(kFrameSamples));
        if (got == 0) break;
        feedFrame(self, frame.data(), static_cast<int>(got));
    }

    // Push-to-talk release: finalize whatever was captured during the hold.
    if (!ptt && ptt_segment) {
        ptt_segment = false;
        if (state == Listen::Command) finishSegment(self, false);
    }
}

// --- AudioService -----------------------------------------------------------

AudioService::AudioService(Database& db, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(db)), db_(db) {}

AudioService::~AudioService() {
    // Ensure workers are torn down if stop() was not called.
    if (d_->asr_thread) {
        d_->asr_thread->quit();
        d_->asr_thread->wait(3000);
        delete d_->asr_worker;
        delete d_->asr_thread;
        d_->asr_worker = nullptr;
        d_->asr_thread = nullptr;
    }
    if (d_->tts_thread) {
        d_->tts_thread->quit();
        d_->tts_thread->wait(3000);
        delete d_->tts_worker;
        delete d_->tts_thread;
        d_->tts_worker = nullptr;
        d_->tts_thread = nullptr;
    }
}

void AudioService::start() {
    Config cfg(db_);
    d_->mic_enabled      = cfg.getBool(keys::MicEnabled);
    d_->ambient_enabled  = cfg.getBool(keys::AmbientTranscription);
    d_->asr_idle_unload_s = cfg.getInt(keys::AudioAsrIdleUnloadS, 90);
    d_->input_device     = cfg.getStr(keys::AudioInputDevice, "");
    d_->output_device    = cfg.getStr(keys::AudioOutputDevice, "");

    // Queued QVector<float> jobs between pump and AsrWorker.
    qRegisterMetaType<QVector<float>>("QVector<float>");

    d_->loadModels();

    // AsrWorker thread — whisper_full never blocks the pump.
    // Threads are not parented to `this` so stop()/dtor can delete them explicitly.
    d_->asr_thread = new QThread();
    d_->asr_worker = new AsrWorker(d_->asr_command, d_->asr_ambient,
                                   d_->cmd_model_path, d_->amb_model_path);
    d_->asr_worker->moveToThread(d_->asr_thread);
    connect(d_->asr_worker, &AsrWorker::finished, this, &AudioService::onAsrFinished,
            Qt::QueuedConnection);
    d_->asr_thread->start();

    // TtsWorker thread — capture ring keeps draining during speech.
    d_->tts_thread = new QThread();
    d_->tts_worker = new TtsWorker(d_->tts);
    d_->tts_worker->moveToThread(d_->tts_thread);
    connect(d_->tts_worker, &TtsWorker::finished, this, &AudioService::onTtsFinished,
            Qt::QueuedConnection);
    d_->tts_thread->start();

    connect(&EventBus::instance(), &EventBus::privacyChanged, this,
            [this](const PrivacyChanged& p) {
                if (p.key == QLatin1String(keys::MicEnabled))
                    setMicEnabled(p.enabled);
                else if (p.key == QLatin1String(keys::AmbientTranscription))
                    setAmbientEnabled(p.enabled);
            });

    d_->timer = new QTimer(this);
    d_->timer->setInterval(40);   // ~25 Hz
    connect(d_->timer, &QTimer::timeout, this, [this] { d_->pump(this); });
    d_->timer->start();

    d_->applyCaptureState();
    PM_INFO("AudioService started: mic={} ambient={} asr_idle_unload={}s ring={}samp",
            d_->mic_enabled, d_->ambient_enabled, d_->asr_idle_unload_s,
            d_->capture.ring().capacity());
}

void AudioService::stop() {
    if (d_->timer) d_->timer->stop();
    d_->stopTts(this);
    d_->capture.stop();

    if (d_->asr_thread) {
        d_->asr_thread->quit();
        d_->asr_thread->wait(5000);
        delete d_->asr_worker;
        d_->asr_worker = nullptr;
        delete d_->asr_thread;
        d_->asr_thread = nullptr;
    }
    if (d_->tts_thread) {
        d_->tts_thread->quit();
        d_->tts_thread->wait(5000);
        delete d_->tts_worker;
        d_->tts_worker = nullptr;
        delete d_->tts_thread;
        d_->tts_thread = nullptr;
    }

    d_->asr_command.unload();
    d_->asr_ambient.unload();
    PM_INFO("AudioService stopped (ring drops={})", d_->capture.ring().drops());
}

void AudioService::speak(const QString& text, const QString& voice) {
    if (text.isEmpty()) return;
    if (!d_->tts.ready()) {
        PM_WARN("audio.tts: not ready; dropping speak request");
        return;
    }

    // Mark speaking so the pump raises barge-in thresholds. Capture stays live.
    d_->speaking.store(true, std::memory_order_release);
    d_->applyBargeInThresholds(true);
    emit listeningStateChanged(false);

    if (d_->tts_worker) {
        QMetaObject::invokeMethod(d_->tts_worker, "process", Qt::QueuedConnection,
                                  Q_ARG(QString, text),
                                  Q_ARG(QString, voice));
    } else {
        // Fallback: block on this thread (tests without full start()).
        d_->tts.speak(text.toStdString(), voice.toStdString());
        d_->speaking.store(false, std::memory_order_release);
        d_->applyBargeInThresholds(false);
    }
}

void AudioService::onAsrFinished(const QString& text, float conf, bool ambient) {
    d_->asr_busy.store(false, std::memory_order_release);
    d_->touchAsrActivity();
    if (text.isEmpty()) return;

    Utterance u;
    u.text       = text.toStdString();
    u.is_ambient = ambient;
    u.confidence = conf;
    u.ts         = Clock::now();
    EventBus::instance().publishUtterance(u);

    Config cfg(db_);
    const int retain_days = cfg.getInt(keys::RetainAmbientDays, 7);
    const int64_t now = to_unix(u.ts);
    const int64_t ttl = (ambient && retain_days > 0) ? now + int64_t(retain_days) * 86400 : 0;
    db_.exec("INSERT INTO transcripts(text,speaker,is_ambient,ttl_at,ts) "
             "VALUES(?1,?2,?3,?4,?5)",
             {u.text, nullptr, ambient ? 1 : 0,
              ttl ? nlohmann::json(ttl) : nlohmann::json(nullptr), now});

    PM_INFO("audio.asr[{}]: \"{}\" (conf {:.2f})",
            ambient ? "ambient" : "command", u.text, conf);
}

void AudioService::onTtsFinished() {
    d_->speaking.store(false, std::memory_order_release);
    d_->applyBargeInThresholds(false);
}

void AudioService::setMicEnabled(bool on) {
    if (d_->mic_enabled == on) return;
    d_->mic_enabled = on;
    db_.setSetting(keys::MicEnabled, on ? "1" : "0");
    d_->applyCaptureState();
    PM_INFO("audio: mic {}", on ? "enabled" : "disabled");
}

void AudioService::setAmbientEnabled(bool on) {
    if (d_->ambient_enabled == on) return;
    d_->ambient_enabled = on;
    db_.setSetting(keys::AmbientTranscription, on ? "1" : "0");
    if (d_->state == Listen::Ambient && !on) {
        d_->segment.clear();
        d_->state = Listen::Wake;
        // Unload ambient model when feature is turned off (on ASR thread).
        if (d_->asr_worker)
            QMetaObject::invokeMethod(d_->asr_worker, "unloadAmbient", Qt::QueuedConnection);
        else
            d_->asr_ambient.unload();
    }
    d_->applyCaptureState();
    PM_INFO("audio: ambient transcription {}", on ? "enabled" : "disabled");
}

void AudioService::pushToTalk(bool down) {
    d_->ptt_down.store(down, std::memory_order_release);
    if (down) {
        // PTT also preloads command ASR (04 §3.2).
        d_->ensureCommandAsr();
        if (d_->speaking.load(std::memory_order_acquire))
            d_->stopTts(this);
    }
    emit listeningStateChanged(down);
}

} // namespace polymath

#include "audio_service.moc"
