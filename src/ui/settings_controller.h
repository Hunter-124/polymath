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
public:
    SettingsController(Database& db, Config& cfg, QObject* parent = nullptr);

    QString accent() const { return accent_; }
    bool    effects() const { return effects_; }
    double  effectsIntensity() const { return effects_intensity_; }
    double  fontScale() const { return font_scale_; }
    bool    reduceMotion() const { return reduce_motion_; }

    void setAccent(const QString& v);
    void setEffects(bool v);
    void setEffectsIntensity(double v);
    void setFontScale(double v);
    void setReduceMotion(bool v);

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

signals:
    void accentChanged();
    void effectsChanged();
    void effectsIntensityChanged();
    void fontScaleChanged();
    void reduceMotionChanged();
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
};

} // namespace polymath
