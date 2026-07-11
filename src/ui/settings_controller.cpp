#include "settings_controller.h"
#include "config.h"
#include "database.h"
#include "event_bus.h"

#include <QMediaDevices>
#include <QAudioDevice>
#include <algorithm>
#include <cmath>
#include <utility>

namespace polymath {

SettingsController::SettingsController(Database& db, Config& cfg, QObject* parent)
    : QObject(parent), db_(db), cfg_(cfg) {
    reloadCache();
}

void SettingsController::reloadCache() {
    accent_ = QString::fromStdString(cfg_.getStr(keys::UiAccent, "#33E1FF"));
    {
        const auto s = cfg_.getStr(keys::UiEffects, "1");
        effects_ = (s == "1" || s == "true" || s == "True");
    }
    try {
        effects_intensity_ = std::stod(cfg_.getStr(keys::UiEffectsIntensity, "0.6"));
    } catch (...) { effects_intensity_ = 0.6; }
    try {
        font_scale_ = std::stod(cfg_.getStr(keys::UiFontScale, "1.0"));
    } catch (...) { font_scale_ = 1.0; }
    {
        const auto s = cfg_.getStr(keys::UiReduceMotion, "0");
        reduce_motion_ = (s == "1" || s == "true");
    }
    tts_engine_ = QString::fromStdString(cfg_.getStr(keys::TtsEngine, "auto"));
    tts_voice_  = QString::fromStdString(cfg_.getStr(keys::TtsVoice, "af_heart"));
    try {
        tts_speed_ = std::stod(cfg_.getStr(keys::TtsSpeed, "1.0"));
    } catch (...) { tts_speed_ = 1.0; }
    try {
        tts_volume_ = std::stod(cfg_.getStr(keys::TtsVolume, "1.0"));
    } catch (...) { tts_volume_ = 1.0; }
}

void SettingsController::writeKey(const QString& key, const QString& value) {
    cfg_.set(key.toUtf8().constData(), value.toStdString());
    emit settingChanged(key, value);
    maybePublishBackendKey(key, value);
}

void SettingsController::maybePublishBackendKey(const QString& key, const QString& value) {
    // Keys with a live backend consumer also publish on EventBus (mirror setPrivacy).
    // tts.speed/tts.volume are deliberately NOT here: AudioService re-reads
    // them from Config on every utterance (see TtsWorker::applyLiveTtsSettings
    // in audio_service.cpp), and routing a slider drag through here would
    // spam a visible toast per tick. tts.engine/tts.voice are infrequent
    // discrete choices, so a toast on change matches the device-picker UX.
    static const char* kBackend[] = {
        keys::WakeWord,
        keys::SearchBackend,
        keys::SearchApiKey,
        keys::AudioInputDevice,
        keys::AudioOutputDevice,
        keys::AudioAsrIdleUnloadS,
        keys::LlmKvQuant,
        keys::TtsEngine,
        keys::TtsVoice,
    };
    for (const char* k : kBackend) {
        if (key == QLatin1String(k)) {
            // Reuse PrivacyChanged for bool-like toggles; for strings publish Notice
            // is too noisy — surface as a typed PrivacyChanged only for wake word
            // presence. Consumers that need the string re-read Config.
            if (key == QLatin1String(keys::WakeWord)) {
                EventBus::instance().publishPrivacy({key, !value.isEmpty()});
            }
            EventBus::instance().publishNotice({
                QStringLiteral("info"),
                QStringLiteral("settings"),
                QStringLiteral("%1 updated").arg(key)
            });
            break;
        }
    }
}

void SettingsController::setAccent(const QString& v) {
    if (accent_ == v) return;
    accent_ = v;
    writeKey(QLatin1String(keys::UiAccent), v);
    emit accentChanged();
}

void SettingsController::setEffects(bool v) {
    if (effects_ == v) return;
    effects_ = v;
    writeKey(QLatin1String(keys::UiEffects), v ? QStringLiteral("1") : QStringLiteral("0"));
    emit effectsChanged();
}

void SettingsController::setEffectsIntensity(double v) {
    v = std::clamp(v, 0.0, 1.0);
    if (std::abs(effects_intensity_ - v) < 1e-6) return;
    effects_intensity_ = v;
    writeKey(QLatin1String(keys::UiEffectsIntensity), QString::number(v, 'f', 3));
    emit effectsIntensityChanged();
}

void SettingsController::setFontScale(double v) {
    v = std::clamp(v, 0.8, 1.4);
    if (std::abs(font_scale_ - v) < 1e-6) return;
    font_scale_ = v;
    writeKey(QLatin1String(keys::UiFontScale), QString::number(v, 'f', 3));
    emit fontScaleChanged();
}

void SettingsController::setReduceMotion(bool v) {
    if (reduce_motion_ == v) return;
    reduce_motion_ = v;
    writeKey(QLatin1String(keys::UiReduceMotion), v ? QStringLiteral("1") : QStringLiteral("0"));
    emit reduceMotionChanged();
}

void SettingsController::setTtsEngine(const QString& v) {
    QString eng = v;
    if (eng != QLatin1String("auto") && eng != QLatin1String("kokoro") &&
        eng != QLatin1String("piper"))
        eng = QStringLiteral("auto");
    if (tts_engine_ == eng) return;
    tts_engine_ = eng;
    writeKey(QLatin1String(keys::TtsEngine), eng);
    emit ttsEngineChanged();
}

void SettingsController::setTtsVoice(const QString& v) {
    if (v.isEmpty() || tts_voice_ == v) return;
    tts_voice_ = v;
    writeKey(QLatin1String(keys::TtsVoice), v);
    emit ttsVoiceChanged();
}

void SettingsController::setTtsSpeed(double v) {
    v = std::clamp(v, 0.8, 1.3);
    if (std::abs(tts_speed_ - v) < 1e-6) return;
    tts_speed_ = v;
    writeKey(QLatin1String(keys::TtsSpeed), QString::number(v, 'f', 3));
    emit ttsSpeedChanged();
}

void SettingsController::setTtsVolume(double v) {
    v = std::clamp(v, 0.0, 1.5);
    if (std::abs(tts_volume_ - v) < 1e-6) return;
    tts_volume_ = v;
    writeKey(QLatin1String(keys::TtsVolume), QString::number(v, 'f', 3));
    emit ttsVolumeChanged();
}

QString SettingsController::getString(const QString& key, const QString& def) const {
    return QString::fromStdString(cfg_.getStr(key.toUtf8().constData(), def.toStdString()));
}

int SettingsController::getInt(const QString& key, int def) const {
    return cfg_.getInt(key.toUtf8().constData(), def);
}

bool SettingsController::getBool(const QString& key, bool def) const {
    const auto s = cfg_.getStr(key.toUtf8().constData(), def ? "1" : "0");
    if (s == "1" || s == "true" || s == "True") return true;
    if (s == "0" || s == "false" || s == "False") return false;
    return def;
}

double SettingsController::getReal(const QString& key, double def) const {
    try {
        return std::stod(cfg_.getStr(key.toUtf8().constData(), std::to_string(def)));
    } catch (...) {
        return def;
    }
}

void SettingsController::setString(const QString& key, const QString& v) {
    writeKey(key, v);
    // Keep typed cache in sync for known UI keys.
    if (key == QLatin1String(keys::UiAccent)) { accent_ = v; emit accentChanged(); }
    else if (key == QLatin1String(keys::UiEffects)) {
        effects_ = (v == QLatin1String("1") || v == QLatin1String("true"));
        emit effectsChanged();
    }
    else if (key == QLatin1String(keys::UiEffectsIntensity)) {
        try { effects_intensity_ = std::stod(v.toStdString()); } catch (...) {}
        emit effectsIntensityChanged();
    }
    else if (key == QLatin1String(keys::UiFontScale)) {
        try { font_scale_ = std::stod(v.toStdString()); } catch (...) {}
        emit fontScaleChanged();
    }
    else if (key == QLatin1String(keys::UiReduceMotion)) {
        reduce_motion_ = (v == QLatin1String("1") || v == QLatin1String("true"));
        emit reduceMotionChanged();
    }
    else if (key == QLatin1String(keys::TtsEngine)) { tts_engine_ = v; emit ttsEngineChanged(); }
    else if (key == QLatin1String(keys::TtsVoice))  { tts_voice_  = v; emit ttsVoiceChanged(); }
    else if (key == QLatin1String(keys::TtsSpeed)) {
        try { tts_speed_ = std::stod(v.toStdString()); } catch (...) {}
        emit ttsSpeedChanged();
    }
    else if (key == QLatin1String(keys::TtsVolume)) {
        try { tts_volume_ = std::stod(v.toStdString()); } catch (...) {}
        emit ttsVolumeChanged();
    }
}

void SettingsController::setInt(const QString& key, int v) {
    setString(key, QString::number(v));
}

void SettingsController::setBool(const QString& key, bool v) {
    setString(key, v ? QStringLiteral("1") : QStringLiteral("0"));
}

void SettingsController::setReal(const QString& key, double v) {
    setString(key, QString::number(v, 'f', 4));
}

static QVariantList devicesOf(const QList<QAudioDevice>& devices) {
    QVariantList out;
    for (const auto& d : devices) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QString::fromUtf8(d.id()));
        m.insert(QStringLiteral("label"), d.description());
        out.push_back(m);
    }
    if (out.isEmpty()) {
        // Always offer a "system default" row so combos are never empty.
        QVariantMap m;
        m.insert(QStringLiteral("id"), QString());
        m.insert(QStringLiteral("label"), QStringLiteral("System default"));
        out.push_back(m);
    }
    return out;
}

QVariantList SettingsController::audioInputDevices() const {
    return devicesOf(QMediaDevices::audioInputs());
}

QVariantList SettingsController::audioOutputDevices() const {
    return devicesOf(QMediaDevices::audioOutputs());
}

QVariantList SettingsController::ttsVoices() const {
    // Mirrors TtsPiper::Impl::shippedKokoroVoices() (tts_piper.cpp) — the
    // full English voice set verified present in the installed
    // data/models/kokoro-engine/voices-v1.0.bin (2026-07-10). Kept as a
    // static list rather than shelling out to `kokoro_worker.py
    // --list-voices` synchronously here, which would block the UI thread on
    // a cold python/venv start every time Settings is opened.
    static const std::pair<const char*, const char*> kVoices[] = {
        {"af_heart",    "Heart (US female, warm)"},
        {"af_bella",    "Bella (US female)"},
        {"af_nova",     "Nova (US female)"},
        {"af_sarah",    "Sarah (US female)"},
        {"af_sky",      "Sky (US female)"},
        {"af_alloy",    "Alloy (US female)"},
        {"af_aoede",    "Aoede (US female)"},
        {"af_jessica",  "Jessica (US female)"},
        {"af_kore",     "Kore (US female)"},
        {"af_nicole",   "Nicole (US female)"},
        {"af_river",    "River (US female)"},
        {"am_adam",     "Adam (US male)"},
        {"am_echo",     "Echo (US male)"},
        {"am_eric",     "Eric (US male)"},
        {"am_fenrir",   "Fenrir (US male)"},
        {"am_liam",     "Liam (US male)"},
        {"am_michael",  "Michael (US male)"},
        {"am_onyx",     "Onyx (US male)"},
        {"am_puck",     "Puck (US male)"},
        {"am_santa",    "Santa (US male)"},
        {"bf_alice",    "Alice (UK female)"},
        {"bf_emma",     "Emma (UK female)"},
        {"bf_isabella", "Isabella (UK female)"},
        {"bf_lily",     "Lily (UK female)"},
        {"bm_daniel",   "Daniel (UK male)"},
        {"bm_fable",    "Fable (UK male)"},
        {"bm_george",   "George (UK male)"},
        {"bm_lewis",    "Lewis (UK male)"},
    };
    QVariantList out;
    for (const auto& [id, label] : kVoices) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QString::fromUtf8(id));
        m.insert(QStringLiteral("label"), QString::fromUtf8(label));
        out.push_back(m);
    }
    return out;
}

void SettingsController::previewVoice(const QString& text) {
    // Route through EventBus::SpeakRequest — the same path AgentLoop uses to
    // speak replies — so Preview exercises the real pipeline (TtsPiper
    // engine selection, mapVoice, chunking, playback), not a side channel.
    // tts_voice_/tts_speed_/tts_volume_ are already persisted by the time the
    // user clicks Preview (each control writes through immediately), and
    // AudioService's TtsWorker re-reads speed/volume/default-voice from
    // Config before every utterance, so this reflects current settings live.
    SpeakRequest req;
    req.text = text.isEmpty()
        ? QStringLiteral("Hi, this is a quick preview of my voice at the current "
                          "speed and volume settings.")
        : text;
    req.voice = tts_voice_;
    req.request_id = QStringLiteral("settings-voice-preview");
    req.append = false;
    req.flush = true;
    EventBus::instance().publishSpeak(req);
}

} // namespace polymath
