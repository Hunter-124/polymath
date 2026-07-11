# D4 — stub notes for EV (`src/ui/tools/capture_views.cpp`)

`SettingsView.qml` now binds new `settings` (SettingsController) surface for the
Voice section. `capture_views.cpp`'s `StubSettings` class (around
`src/ui/tools/capture_views.cpp:195-246`) needs the following additions or the
headless capture of `SettingsView` will crash on unresolved properties/methods.

## New Q_PROPERTY (mirror the existing `accent`/`effectsIntensity` pattern)

```cpp
Q_PROPERTY(QString ttsEngine READ ttsEngine WRITE setTtsEngine NOTIFY ttsEngineChanged)
Q_PROPERTY(QString ttsVoice  READ ttsVoice  WRITE setTtsVoice  NOTIFY ttsVoiceChanged)
Q_PROPERTY(double  ttsSpeed  READ ttsSpeed  WRITE setTtsSpeed  NOTIFY ttsSpeedChanged)
Q_PROPERTY(double  ttsVolume READ ttsVolume WRITE setTtsVolume NOTIFY ttsVolumeChanged)
```

Suggested stub bodies (same shape as `accent_`/`intensity_` fields already there):

```cpp
QString ttsEngine() const { return tts_engine_; }
QString ttsVoice() const { return tts_voice_; }
double  ttsSpeed() const { return tts_speed_; }
double  ttsVolume() const { return tts_volume_; }
void setTtsEngine(const QString& v) { if (tts_engine_ == v) return; tts_engine_ = v; emit ttsEngineChanged(); emit settingChanged("tts.engine", v); }
void setTtsVoice(const QString& v) { if (tts_voice_ == v) return; tts_voice_ = v; emit ttsVoiceChanged(); emit settingChanged("tts.voice", v); }
void setTtsSpeed(double v) { if (tts_speed_ == v) return; tts_speed_ = v; emit ttsSpeedChanged(); }
void setTtsVolume(double v) { if (tts_volume_ == v) return; tts_volume_ = v; emit ttsVolumeChanged(); }
```

Signals to add:

```cpp
void ttsEngineChanged();
void ttsVoiceChanged();
void ttsSpeedChanged();
void ttsVolumeChanged();
```

Private fields:

```cpp
QString tts_engine_ = QStringLiteral("auto");
QString tts_voice_  = QStringLiteral("af_heart");
double  tts_speed_  = 1.0;
double  tts_volume_ = 1.0;
```

## New Q_INVOKABLE

```cpp
Q_INVOKABLE QVariantList ttsVoices() const {
    // A handful of fake rows is enough for the capture to render a populated
    // combo — the real list (28 shipped Kokoro voices) lives in
    // SettingsController::ttsVoices() (src/ui/settings_controller.cpp).
    return {
        QVariantMap{{"id","af_heart"},{"label","Heart (US female, warm)"}},
        QVariantMap{{"id","am_adam"},{"label","Adam (US male)"}},
        QVariantMap{{"id","bf_emma"},{"label","Emma (UK female)"}},
    };
}
Q_INVOKABLE void previewVoice(const QString& = QString()) { /* no-op in capture */ }
```

## Capture concerns

- `SettingsView.qml`'s new Voice section (`secVoice`, between Audio and Web
  Search) calls `settings.ttsVoices()` in `Component.onCompleted` via
  `refreshTtsVoices()` — must return a non-empty list or the voice
  `PmComboBox` renders with an empty model (visually fine, just an empty
  combo — won't crash, but the capture PNG will look odd/incomplete without
  the stub above).
- `settings.ttsEngine` / `settings.ttsVoice` / `settings.ttsSpeed` /
  `settings.ttsVolume` are read directly (typed `Q_PROPERTY`, no `getString`
  fallback) by the new section — unlike the free-form keys at the top of
  `SettingsView.qml`, these do NOT go through `settings.getString(...)`, so
  the stub must expose them as real properties (not just generic
  get/set-by-key) or QML property binding to `settings.ttsEngine` etc. will
  fail to resolve at capture time.
- No new `views[]` entry is required — `SettingsView` is already captured;
  this only adds bound surface inside the existing view.
- `scrollToSection("voice")` now resolves to the new section (harmless if
  unused by capture_views' seed data).
