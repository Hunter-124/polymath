#pragma once
//
// SettingsController — QML-facing settings facade (context property "settings").
// Owns typed UI props + generic get*/set* over Config/Database. UI-thread only.
// Spec: docs/overhaul/02_GUI_FEATURES.md §Feature 1.
//
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

namespace polymath {

class Database;
class Config;

class SettingsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent           READ accent           WRITE setAccent
               NOTIFY accentChanged)
    Q_PROPERTY(bool    effects          READ effects          WRITE setEffects
               NOTIFY effectsChanged)
    Q_PROPERTY(double  effectsIntensity READ effectsIntensity WRITE setEffectsIntensity
               NOTIFY effectsIntensityChanged)
    Q_PROPERTY(double  fontScale        READ fontScale        WRITE setFontScale
               NOTIFY fontScaleChanged)
    Q_PROPERTY(bool    reduceMotion     READ reduceMotion     WRITE setReduceMotion
               NOTIFY reduceMotionChanged)
    // D4: Voice section (tts.engine/voice/speed/volume).
    Q_PROPERTY(QString ttsEngine        READ ttsEngine        WRITE setTtsEngine
               NOTIFY ttsEngineChanged)
    Q_PROPERTY(QString ttsVoice         READ ttsVoice         WRITE setTtsVoice
               NOTIFY ttsVoiceChanged)
    Q_PROPERTY(double  ttsSpeed         READ ttsSpeed         WRITE setTtsSpeed
               NOTIFY ttsSpeedChanged)
    Q_PROPERTY(double  ttsVolume        READ ttsVolume        WRITE setTtsVolume
               NOTIFY ttsVolumeChanged)
public:
    SettingsController(Database& db, Config& cfg, QObject* parent = nullptr);

    QString accent() const { return accent_; }
    bool    effects() const { return effects_; }
    double  effectsIntensity() const { return effects_intensity_; }
    double  fontScale() const { return font_scale_; }
    bool    reduceMotion() const { return reduce_motion_; }
    QString ttsEngine() const { return tts_engine_; }
    QString ttsVoice() const { return tts_voice_; }
    double  ttsSpeed() const { return tts_speed_; }
    double  ttsVolume() const { return tts_volume_; }

    void setAccent(const QString& v);
    void setEffects(bool v);
    void setEffectsIntensity(double v);
    void setFontScale(double v);
    void setReduceMotion(bool v);
    void setTtsEngine(const QString& v);
    void setTtsVoice(const QString& v);
    void setTtsSpeed(double v);
    void setTtsVolume(double v);

    Q_INVOKABLE QString getString(const QString& key, const QString& def = {}) const;
    Q_INVOKABLE int     getInt(const QString& key, int def = 0) const;
    Q_INVOKABLE bool    getBool(const QString& key, bool def = false) const;
    Q_INVOKABLE double  getReal(const QString& key, double def = 0.0) const;
    Q_INVOKABLE void    setString(const QString& key, const QString& v);
    Q_INVOKABLE void    setInt(const QString& key, int v);
    Q_INVOKABLE void    setBool(const QString& key, bool v);
    Q_INVOKABLE void    setReal(const QString& key, double v);

    Q_INVOKABLE QVariantList audioInputDevices() const;
    Q_INVOKABLE QVariantList audioOutputDevices() const;

    // Shipped Kokoro voices (af_*/am_*/bf_*/bm_* — the set TtsPiper::mapVoice
    // validates against) as [{id,label}], for the Settings > Voice combo.
    // Static list rather than shelling out to `kokoro_worker.py --list-voices`
    // synchronously on the UI thread (would stall opening Settings on a cold
    // venv/python start).
    Q_INVOKABLE QVariantList ttsVoices() const;

    // Speaks a short test line (or `text` if given) through the real TTS
    // pipeline via EventBus SpeakRequest — same path the agent uses — so the
    // Preview button reflects the just-saved engine/voice/speed/volume.
    Q_INVOKABLE void previewVoice(const QString& text = QString());

signals:
    void accentChanged();
    void effectsChanged();
    void effectsIntensityChanged();
    void fontScaleChanged();
    void reduceMotionChanged();
    void ttsEngineChanged();
    void ttsVoiceChanged();
    void ttsSpeedChanged();
    void ttsVolumeChanged();
    void settingChanged(QString key, QVariant value);

private:
    void reloadCache();
    void writeKey(const QString& key, const QString& value);
    void maybePublishBackendKey(const QString& key, const QString& value);

    Database& db_;
    Config&   cfg_;
    QString   accent_ = QStringLiteral("#33E1FF");
    bool      effects_ = true;
    double    effects_intensity_ = 0.6;
    double    font_scale_ = 1.0;
    bool      reduce_motion_ = false;
    QString   tts_engine_ = QStringLiteral("auto");
    QString   tts_voice_  = QStringLiteral("af_heart");
    double    tts_speed_  = 1.0;
    double    tts_volume_ = 1.0;
};

} // namespace polymath
