#include "settings_controller.h"
#include "config.h"
#include "database.h"
#include "event_bus.h"

#include <QMediaDevices>
#include <QAudioDevice>
#include <algorithm>
#include <cmath>

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
}

void SettingsController::writeKey(const QString& key, const QString& value) {
    cfg_.set(key.toUtf8().constData(), value.toStdString());
    emit settingChanged(key, value);
    maybePublishBackendKey(key, value);
}

void SettingsController::maybePublishBackendKey(const QString& key, const QString& value) {
    // Keys with a live backend consumer also publish on EventBus (mirror setPrivacy).
    static const char* kBackend[] = {
        keys::WakeWord,
        keys::SearchBackend,
        keys::SearchApiKey,
        keys::AudioInputDevice,
        keys::AudioOutputDevice,
        keys::AudioAsrIdleUnloadS,
        keys::LlmKvQuant,
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

} // namespace polymath
