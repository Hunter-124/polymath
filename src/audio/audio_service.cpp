#include "audio_service.h"
#include "database.h"
#include "event_bus.h"
#include "config.h"
#include "logging.h"

// Wave-0 compiling stub. Wave-1 audio agent implements Impl with miniaudio
// capture, openWakeWord, Silero VAD, whisper.cpp ASR, and Piper TTS (files:
// capture.cpp wakeword.cpp vad.cpp asr_whisper.cpp tts_piper.cpp).

namespace polymath {

struct AudioService::Impl {
    bool mic_enabled = true;
    bool ambient_enabled = true;
};

AudioService::AudioService(Database& db, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()), db_(db) {}
AudioService::~AudioService() = default;

void AudioService::start() {
    Config cfg(db_);
    d_->mic_enabled     = cfg.getBool(keys::MicEnabled);
    d_->ambient_enabled = cfg.getBool(keys::AmbientTranscription);
    PM_INFO("AudioService started (stub): mic={} ambient={}", d_->mic_enabled, d_->ambient_enabled);
}
void AudioService::stop() {}

void AudioService::speak(const QString& text, const QString& voice) {
    PM_INFO("TTS(stub) [{}]: {}", voice.toStdString(), text.toStdString());
}
void AudioService::setMicEnabled(bool on)     { d_->mic_enabled = on; }
void AudioService::setAmbientEnabled(bool on) { d_->ambient_enabled = on; }
void AudioService::pushToTalk(bool down)      { emit listeningStateChanged(down); }

} // namespace polymath
