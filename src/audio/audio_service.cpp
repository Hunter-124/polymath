#include "audio_service.h"
#include "database.h"
#include "event_bus.h"
#include "config.h"
#include "paths.h"
#include "logging.h"

#include "audio_common.h"
#include "capture.h"
#include "network_audio_source.h"
#include "wakeword.h"
#include "vad.h"
#include "asr_whisper.h"
#include "tts_piper.h"

#include <QTimer>
#include <QString>

#include <atomic>
#include <deque>
#include <vector>

// AudioService pimpl: drives capture -> wake word -> VAD -> ASR -> Utterance on
// the service's own QThread, plus speak() -> Piper TTS. A QTimer running on the
// worker thread's event loop pumps the pipeline so queued slot calls (speak,
// setMicEnabled, pushToTalk, privacy changes) are still delivered between ticks.

namespace polymath {

using namespace polymath::audio;

namespace {
// How the pipeline is currently listening.
enum class Listen { Idle, Wake, Command, Ambient };

// Segment length guards (seconds @ 16 kHz) to bound a single utterance.
constexpr int kMaxCommandSec = 15;
constexpr int kMaxAmbientSec = 30;
constexpr int kPreRollMs     = 300;   // audio kept before a VAD speech-start
}

struct AudioService::Impl {
    explicit Impl(Database& db) : db(db) {}

    Database& db;

    // Pipeline stages.
    Capture     capture;
    WakeWord    wake;
    Vad         vad;
    AsrWhisper  asr_command;
    AsrWhisper  asr_ambient;
    TtsPiper    tts;

    // Network mic input (Wi-Fi satellites). Owns a dedicated SPSC ring so the
    // Capture SPSC invariant is not violated. pump() drains both rings.
    FloatRing                             net_ring{1u << 16};  // ~4 s @ 16 kHz
    std::unique_ptr<NetworkAudioSource>   net_source;          // null when disabled
    // last room_id tag seen from the network; "" when local mic is active.
    std::string                           active_source;

    // Worker-thread pump.
    QTimer* timer = nullptr;

    // Privacy / mode flags (worker-thread only after start()).
    bool mic_enabled     = true;
    bool ambient_enabled = false;
    std::atomic<bool> ptt_down{false};   // set from queued slot, read in pump
    std::atomic<bool> speaking{false};   // pause capture-driven ASR during TTS

    Listen state       = Listen::Idle;
    bool   ptt_segment = false;   // current Command segment opened by push-to-talk

    // Scratch buffers.
    std::vector<float> frame;        // 1280-sample drain buffer
    std::deque<float>  preroll;      // ring of recent audio for pre-VAD context
    std::vector<float> segment;      // active speech segment being recorded
    std::vector<float> vad_window;   // 512-sample accumulator for Silero

    int  command_timeout_frames = 0; // frames left to wait for speech after wake

    // --- helpers ---
    void loadModels();
    void applyCaptureState();        // start/stop device per mic + ptt + ambient
    void pump(AudioService* self);   // one timer tick
    void feedFrame(AudioService* self, const float* data, int n);
    void finishSegment(AudioService* self, bool ambient);
};

void AudioService::Impl::loadModels() {
    const auto models = Paths::instance().models();
    Config cfg(db);

    // Wake word: phrase from config (default hey_jarvis); models under wakeword/.
    const std::string phrase = cfg.getStr(keys::WakeWord, "hey_jarvis");
    wake.load(models / "wakeword", phrase);

    // Silero VAD.
    vad.load(models / "vad" / "silero_vad.onnx");

    // ASR: command (base.en) + ambient (tiny.en). Either may be absent.
    asr_command.load(models / "whisper" / "ggml-base.en.bin", AsrWhisper::Mode::Command);
    asr_ambient.load(models / "whisper" / "ggml-tiny.en.bin", AsrWhisper::Mode::Ambient);

    // TTS voices. Active personality voice would override the default; we read a
    // sensible default here and honour per-request voice in speak().
    tts.init(models / "piper", "en_US-amy-medium");

    capture.init();
}

void AudioService::Impl::applyCaptureState() {
    // Capture runs whenever the mic is enabled. With the mic off we fully stop
    // the device so the OS indicator clears (privacy contract).
    const bool want = mic_enabled;
    if (want && !capture.isRunning()) {
        if (capture.start()) { wake.reset(); vad.reset(); preroll.clear(); }
    } else if (!want && capture.isRunning()) {
        capture.stop();
        state = Listen::Idle;
        segment.clear();
        preroll.clear();
    }

    // Choose the resting listen state: ambient continuous transcription when
    // enabled, otherwise wait for the wake word.
    if (want) {
        if (state == Listen::Idle)
            state = ambient_enabled ? Listen::Ambient : Listen::Wake;
        else if (state == Listen::Wake && ambient_enabled)
            state = Listen::Ambient;
        else if (state == Listen::Ambient && !ambient_enabled)
            state = Listen::Wake;
    }
}

void AudioService::Impl::feedFrame(AudioService* self, const float* data, int n) {
    // Maintain pre-roll (recent audio) so a segment includes the word onset.
    const size_t preroll_max = static_cast<size_t>(kSampleRate) * kPreRollMs / 1000;
    for (int i = 0; i < n; ++i) {
        preroll.push_back(data[i]);
        if (preroll.size() > preroll_max) preroll.pop_front();
    }

    // Wake-word detection only matters while waiting (Wake state).
    if (state == Listen::Wake && wake.ready()) {
        float score = 0.0f;
        if (wake.process(data, n, &score)) {
            emit self->wakeWordHeard();
            emit self->listeningStateChanged(true);
            EventBus::instance().publishWakeWord(
                {QString::fromStdString(wake.phrase()), to_unix(Clock::now())});
            // Transition to command capture; seed segment with pre-roll.
            state = Listen::Command;
            command_timeout_frames = kSampleRate / kFrameSamples * 5; // 5 s to start
            segment.assign(preroll.begin(), preroll.end());
            vad.reset();
            vad_window.clear();
        }
        return;
    }

    // While capturing a command, or doing ambient transcription, gate on VAD.
    if (state == Listen::Command || state == Listen::Ambient) {
        const Listen seg_state = state;
        segment.insert(segment.end(), data, data + n);

        // Run VAD on 512-sample windows. A SpeechEnd finishes the current
        // segment (swapping out `segment`/`vad_window`); stop processing the
        // remaining windows of this frame in that case — they are post-speech
        // silence and would otherwise index a buffer finishSegment() cleared.
        vad_window.insert(vad_window.end(), data, data + n);
        size_t off = 0;
        bool finished = false;
        while (!finished && vad_window.size() - off >= 512) {
            float prob = 0.0f;
            Vad::Event ev = vad.process(vad_window.data() + off, &prob);
            off += 512;
            if (ev == Vad::Event::SpeechEnd) {
                finishSegment(self, seg_state == Listen::Ambient);
                finished = true;
            }
        }
        if (finished) return;                       // segment closed this frame
        if (off) vad_window.erase(vad_window.begin(), vad_window.begin() + off);

        // Safety cap on segment length.
        const int cap = (state == Listen::Command ? kMaxCommandSec : kMaxAmbientSec);
        if (static_cast<int>(segment.size()) > cap * kSampleRate) {
            finishSegment(self, state == Listen::Ambient);
            return;
        }

        // Wake-driven command: if speech never started, time out back to wake.
        // Push-to-talk segments stay open until the button is released.
        if (state == Listen::Command && !ptt_segment && !vad.inSpeech()) {
            command_timeout_frames -= (n / kFrameSamples) + 1;
            if (command_timeout_frames <= 0) {
                PM_DEBUG("audio: command timed out waiting for speech");
                segment.clear();
                state = ambient_enabled ? Listen::Ambient : Listen::Wake;
                emit self->listeningStateChanged(false);
            }
        }
    }
}

void AudioService::Impl::finishSegment(AudioService* self, bool ambient) {
    std::vector<float> pcm;
    pcm.swap(segment);
    vad.reset();
    vad_window.clear();

    // Reset listen state: ambient keeps listening; command returns to wake/ambient.
    if (!ambient) {
        ptt_segment = false;
        emit self->listeningStateChanged(false);
        state = ambient_enabled ? Listen::Ambient : Listen::Wake;
    }

    // Ignore too-short blips (< 200 ms of audio).
    if (pcm.size() < static_cast<size_t>(kSampleRate) / 5) return;

    AsrWhisper& asr = ambient ? asr_ambient : asr_command;
    if (!asr.ready()) return;

    // whisper_full is synchronous; it blocks this worker (never the UI) thread.
    // Captured audio buffers in the ring during the stall and is drained next
    // pump; the pump's speaking/clear guards keep the latency bounded.
    float conf = 0.0f;
    const std::string text = asr.transcribe(pcm, &conf);
    if (text.empty()) return;

    Utterance u;
    u.text       = text;
    u.is_ambient = ambient;
    u.confidence = conf;
    u.source     = active_source;  // "" = local mic; else satellite room id
    u.ts         = Clock::now();
    EventBus::instance().publishUtterance(u);

    // Persist to the transcripts table with retention TTL.
    Config cfg(db);
    const int retain_days = cfg.getInt(keys::RetainAmbientDays, 7);
    const int64_t now = to_unix(u.ts);
    const int64_t ttl = (ambient && retain_days > 0) ? now + int64_t(retain_days) * 86400 : 0;
    db.exec("INSERT INTO transcripts(text,speaker,is_ambient,ttl_at,ts) "
            "VALUES(?1,?2,?3,?4,?5)",
            {text, nullptr, ambient ? 1 : 0,
             ttl ? nlohmann::json(ttl) : nlohmann::json(nullptr), now});

    PM_INFO("audio.asr[{}]: \"{}\" (conf {:.2f})",
            ambient ? "ambient" : "command", text, conf);
}

void AudioService::Impl::pump(AudioService* self) {
    const bool has_local = capture.isRunning();
    const bool has_net   = net_source && net_source->isRunning();
    if (!has_local && !has_net) return;

    if (speaking.load(std::memory_order_acquire)) {
        // Discard audio captured during our own TTS to avoid self-transcription.
        if (has_local) capture.ring().clear();
        if (has_net)   net_ring.clear();
        return;
    }

    // Push-to-talk press: open the mic without a wake word.
    const bool ptt = ptt_down.load(std::memory_order_acquire);
    if (ptt && !ptt_segment) {
        ptt_segment = true;
        state = Listen::Command;
        segment.assign(preroll.begin(), preroll.end());
        vad.reset();
        vad_window.clear();
        emit self->listeningStateChanged(true);
    }

    frame.resize(kFrameSamples);

    // Drain the local capture ring (source = local mic, "").
    if (has_local) {
        active_source = "";
        while (capture.ring().available() >= static_cast<size_t>(kFrameSamples)) {
            size_t got = capture.ring().read(frame.data(), kFrameSamples);
            if (got == 0) break;
            feedFrame(self, frame.data(), static_cast<int>(got));
        }
    }

    // Drain the network ring (source = room id from last seen datagram).
    if (has_net && net_ring.available() >= static_cast<size_t>(kFrameSamples)) {
        active_source = std::to_string(net_source->lastRoomId());
        while (net_ring.available() >= static_cast<size_t>(kFrameSamples)) {
            size_t got = net_ring.read(frame.data(), kFrameSamples);
            if (got == 0) break;
            feedFrame(self, frame.data(), static_cast<int>(got));
        }
        active_source = "";  // reset after network batch
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

AudioService::~AudioService() = default;

void AudioService::start() {
    Config cfg(db_);
    d_->mic_enabled     = cfg.getBool(keys::MicEnabled);
    d_->ambient_enabled = cfg.getBool(keys::AmbientTranscription);

    // Network mic input (Wi-Fi satellites). Off by default; no port is opened
    // unless explicitly enabled in config.
    if (cfg.getInt(keys::NetworkMicsEnabled, 0) != 0) {
        d_->net_source = std::make_unique<audio::NetworkAudioSource>(d_->net_ring);
        // Track room changes so active_source stays current between frames.
        d_->net_source->setRoomCallback([impl = d_.get()](uint8_t room_id) {
            // Runs on the rx thread. Atomic store; worker reads in pump().
            // active_source is set per-batch in pump; this is informational.
            (void)room_id;
        });
        if (!d_->net_source->start()) {
            PM_WARN("audio: NetworkAudioSource failed to bind; network mics disabled");
            d_->net_source.reset();
        }
    }

    d_->loadModels();

    // React to privacy toggles published by the Settings UI (queued onto this
    // thread). Mic + ambient changes adjust capture/listen state live.
    connect(&EventBus::instance(), &EventBus::privacyChanged, this,
            [this](const PrivacyChanged& p) {
                if (p.key == QLatin1String(keys::MicEnabled))            setMicEnabled(p.enabled);
                else if (p.key == QLatin1String(keys::AmbientTranscription)) setAmbientEnabled(p.enabled);
            });

    // Pump timer lives on this worker thread.
    d_->timer = new QTimer(this);
    d_->timer->setInterval(40);   // ~25 Hz; matches 2x 80 ms frames of headroom
    connect(d_->timer, &QTimer::timeout, this, [this] { d_->pump(this); });
    d_->timer->start();

    d_->applyCaptureState();
    PM_INFO("AudioService started: mic={} ambient={}", d_->mic_enabled, d_->ambient_enabled);
}

void AudioService::stop() {
    if (d_->timer) { d_->timer->stop(); }
    d_->capture.stop();
    if (d_->net_source) d_->net_source->stop();
    PM_INFO("AudioService stopped");
}

void AudioService::speak(const QString& text, const QString& voice) {
    const std::string t = text.toStdString();
    const std::string v = voice.toStdString();
    if (t.empty()) return;

    if (!d_->tts.ready()) {
        PM_WARN("audio.tts: not ready; dropping speak request");
        return;
    }
    // Pause capture-driven ASR for the duration of playback (barge-in guard).
    d_->speaking.store(true, std::memory_order_release);
    emit listeningStateChanged(false);
    emit speakingStateChanged(true);    // -> UI talking avatar comes alive
    d_->tts.speak(t, v);
    d_->capture.ring().clear();
    d_->net_ring.clear();   // discard satellite audio captured during our own TTS
    d_->speaking.store(false, std::memory_order_release);
    emit speakingStateChanged(false);
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
    // If we were ambient-listening and it's now off, fall back to wake.
    if (d_->state == Listen::Ambient && !on) {
        d_->segment.clear();
        d_->state = Listen::Wake;
    }
    d_->applyCaptureState();
    PM_INFO("audio: ambient transcription {}", on ? "enabled" : "disabled");
}

void AudioService::pushToTalk(bool down) {
    d_->ptt_down.store(down, std::memory_order_release);
    emit listeningStateChanged(down);
}

} // namespace polymath
