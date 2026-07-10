#pragma once
//
// AudioService — full voice pipeline (04 §3 rework):
//   WASAPI capture → Silero VAD (always-on gate) → gated openWakeWord
//   → segment bookkeeping on the pump thread;
//   whisper_full on a dedicated AsrWorker QThread;
//   Piper TTS + playback on a dedicated TtsWorker QThread.
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
    // EventBus::speakRequested. append=true streams more audio; flush=true waits
    // for the playback queue to drain (end of a streamed reply).
    void speak(const QString& text, const QString& voice,
               bool append = false, bool flush = false);
    void setMicEnabled(bool on);
    void setAmbientEnabled(bool on);
    void pushToTalk(bool down);   // UI button: open mic without wake word

signals:
    void listeningStateChanged(bool listening);
    void wakeWordHeard();

    // Internal: post a finished speech segment to the AsrWorker thread.
    void asrJobQueued(const QVector<float>& pcm, bool ambient);
    // Internal: post a speak request to the TtsWorker thread.
    void ttsJobQueued(const QString& text, const QString& voice, bool append, bool flush);

private slots:
    void onAsrFinished(const QString& text, float conf, bool ambient);
    void onTtsFinished();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    Database& db_;
};

} // namespace polymath
