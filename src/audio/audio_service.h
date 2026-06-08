#pragma once
//
// AudioService — the full voice pipeline on one worker thread:
//   WASAPI capture (miniaudio) -> wake word (openWakeWord/ONNX)
//   -> VAD (Silero/ONNX) -> ASR (whisper.cpp) -> EventBus::utterance
// and the playback side: EventBus::speakRequested -> Piper TTS -> device.
//
// Privacy: capture stops when privacy.mic_enabled is off; ambient continuous
// ASR runs only when privacy.ambient_transcription is on.
//
#include "service.h"
#include <QObject>
#include <memory>
#include <string>

namespace polymath {

class Database;

class AudioService : public QObject, public IService {
    Q_OBJECT
public:
    explicit AudioService(Database& db, QObject* parent = nullptr);
    ~AudioService() override;

    void start() override;
    void stop() override;
    const char* serviceName() const override { return "audio"; }

public slots:
    void speak(const QString& text, const QString& voice);  // EventBus::speakRequested
    void setMicEnabled(bool on);
    void setAmbientEnabled(bool on);
    void pushToTalk(bool down);   // UI button: open mic without wake word

signals:
    void listeningStateChanged(bool listening);
    void wakeWordHeard();

private:
    struct Impl;                       // pimpl: capture/wakeword/vad/asr/tts
    std::unique_ptr<Impl> d_;
    Database& db_;
};

} // namespace polymath
