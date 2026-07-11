// capture_views — offscreen screenshot harness for the Polymath QML views.
//
// Renders the Main shell and each of the 9 views to PNG without a display and
// without booting the real backend.  It installs a *stub* `app` context object
// and stub list models that mimic the AppController surface the views bind to
// (properties, invokables, role-named list models), seeded with representative
// data so empty AND populated states can be captured deterministically.
//
// Run headless:
//   set QT_QPA_PLATFORM=offscreen
//   capture_views.exe <output-dir> [--empty]
//
// --empty seeds nothing (exercises the empty/first-run states); the default
// seeds populated data.  This lives under src/ui because Card F owns only the
// UI; it deliberately does NOT link pm_app, so no service threads / models /
// GPU are touched.
//
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickItemGrabResult>
#include <QQuickGraphicsConfiguration>
#include <QAbstractListModel>
#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QDir>
#include <QImage>
#include <QEventLoop>
#include <QTimer>
#include <QFontDatabase>
#include <QFont>
#include <QDebug>
#include <vector>

// ---------------------------------------------------------------------------
// A generic role-named list model: feed it role names + a list of row maps.
// Mirrors the QAbstractListModels the real app exposes (chatModel, taskModel…)
// closely enough that the view delegates' `required property <role>` bindings
// resolve exactly as they do against the C++ models.
// ---------------------------------------------------------------------------
class StubListModel : public QAbstractListModel {
public:
    StubListModel(QStringList roles, QVariantList rows, QObject* parent = nullptr)
        : QAbstractListModel(parent), rows_(std::move(rows)) {
        int r = Qt::UserRole + 1;
        for (const auto& name : roles) roles_.insert(r++, name.toUtf8());
    }
    int rowCount(const QModelIndex& = {}) const override { return rows_.size(); }
    QVariant data(const QModelIndex& idx, int role) const override {
        if (idx.row() < 0 || idx.row() >= rows_.size()) return {};
        const auto key = roles_.value(role);
        return rows_.at(idx.row()).toMap().value(QString::fromUtf8(key));
    }
    QHash<int, QByteArray> roleNames() const override { return roles_; }

    // Invokables the views call on the model directly (no-ops for capture).
    Q_INVOKABLE void refresh() {}
    Q_INVOKABLE void addItem(const QString&, const QString& = {}) {}
    Q_INVOKABLE void setDone(int, bool) {}
    Q_INVOKABLE void removeItem(int) {}
    Q_INVOKABLE void clearDone() {}
    Q_INVOKABLE void setFilter(const QString&) {}
    Q_INVOKABLE void setEnabled(int, bool) {}  // D1 ScheduledGoalsModel
    // E2 PersonalityModel: name-keyed lookup + full row map for editor seed.
    Q_INVOKABLE int indexOfName(const QString& name) const {
        for (int i = 0; i < rows_.size(); ++i)
            if (rows_.at(i).toMap().value(QStringLiteral("name")).toString() == name)
                return i;
        return -1;
    }
    Q_INVOKABLE QVariantMap get(int row) const {
        if (row < 0 || row >= rows_.size()) return {};
        return rows_.at(row).toMap();
    }
    // Dashboard HUD: count rows whose "status" role matches (case-sensitive).
    Q_INVOKABLE int countByStatus(const QString& status) const {
        int n = 0;
        for (const auto& row : rows_) {
            if (row.toMap().value(QStringLiteral("status")).toString() == status)
                ++n;
        }
        return n;
    }
    // C4 SessionsModel surface (AgentSessionsView / Dashboard agent count).
    Q_INVOKABLE QString spawn(const QString&, const QString&, const QString&,
                              const QString& = {}) { return {}; }
    Q_INVOKABLE void send(const QString&, const QString&) {}
    Q_INVOKABLE void stop(const QString&) {}
    Q_INVOKABLE void clearPing(const QString&) {}
    Q_INVOKABLE QVariantList availableProviders() const {
        return {
            QVariantMap{{"name", "claude-code"}, {"available", true}, {"experimental", false}},
            QVariantMap{{"name", "codex"}, {"available", false}, {"experimental", true}},
            QVariantMap{{"name", "pty"}, {"available", true}, {"experimental", false}},
        };
    }
    Q_INVOKABLE QStringList eventLog(const QString&) const {
        return {QStringLiteral("Started: session started"),
                QStringLiteral("AssistantText: working…")};
    }
    Q_INVOKABLE QString lastError() const { return {}; }

private:
    QHash<int, QByteArray> roles_;
    QVariantList rows_;
    Q_OBJECT
    Q_PROPERTY(QString filter MEMBER filter_)
    Q_PROPERTY(int count READ rowCount CONSTANT)
    QString filter_;
};

// ---------------------------------------------------------------------------
// Stub `app` — the AppController facade surface the QML binds to.
// ---------------------------------------------------------------------------
class StubApp : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
    Q_PROPERTY(QString activePersonality READ activePersonality NOTIFY changed)
    Q_PROPERTY(QString modelStatus READ modelStatus NOTIFY changed)
    Q_PROPERTY(bool hasModels READ hasModels NOTIFY changed)
    Q_PROPERTY(bool firstRun READ firstRun NOTIFY changed)
    Q_PROPERTY(QObject* chatModel READ chatModel CONSTANT)
    Q_PROPERTY(int vramUsedMiB READ vramUsedMiB NOTIFY vramChanged)
    Q_PROPERTY(int vramTotalMiB READ vramTotalMiB NOTIFY vramChanged)
public:
    bool populated = true;

    bool listening() const { return populated; }
    QString activePersonality() const { return "Marcus Aurelius"; }
    QString modelStatus() const {
        return populated ? "fast: gemma-3n-E4B-it-Q4_K_M" : "no model loaded";
    }
    bool hasModels() const { return populated; }
    bool firstRun() const { return !populated; }
    QObject* chatModel() const { return chat_; }
    void setChat(QObject* m) { chat_ = m; }
    int vramUsedMiB() const { return populated ? 5400 : 0; }
    int vramTotalMiB() const { return 8192; }

    Q_INVOKABLE QStringList personalities() const {
        return populated ? QStringList{"Assistant", "Marcus Aurelius", "Ada Lovelace"}
                         : QStringList{"Assistant"};
    }
    Q_INVOKABLE QVariantList models() const {
        QVariantList out;
        if (!populated) return out;
        auto mk = [](QString name, QString role, int ctx, int ngl, bool act) {
            QVariantMap m;
            m["id"] = name; m["displayName"] = name; m["role"] = role; m["nCtx"] = ctx;
            m["nGpuLayers"] = ngl; m["active"] = act;
            m["path"] = "data/models/" + name + ".gguf";
            return QVariant(m);
        };
        out << mk("gemma-3n-E4B-it-Q4_K_M", "fast", 4096, 999, true);
        out << mk("gemma-3-27b-it-Q4_K_M", "heavy", 4096, 46, false);
        out << mk("gemma-3-4b-it-Q4_K_M", "vision", 4096, 999, false);
        out << mk("embeddinggemma-Q8_0", "embedding", 2048, 0, false);
        return out;
    }
    Q_INVOKABLE bool privacy(const QString& key) const {
        // Sensible first-run defaults: senses opt-in OFF, encrypt ON.
        if (!populated) return key == "privacy.encrypt_at_rest";
        return key != "privacy.face_recognition";
    }
    // Action stubs the views wire to (no-ops for capture).
    Q_INVOKABLE void sendChat(const QString&) {}
    Q_INVOKABLE void sendText(const QString&) {}
    Q_INVOKABLE void pushToTalk(bool) {}
    Q_INVOKABLE void setPersonality(const QString&) {}
    Q_INVOKABLE void setPrivacy(const QString&, bool) {}
    Q_INVOKABLE void findObject(const QString&) {}
    Q_INVOKABLE void addShoppingItem(const QString&) {}
    Q_INVOKABLE void refreshAll() {}
    Q_INVOKABLE void refreshShopping() {}
    Q_INVOKABLE void refreshCameras() {}
    Q_INVOKABLE void refreshTasks() {}
    Q_INVOKABLE void refreshSchedules() {}  // D1 ScheduledGoalsModel
    Q_INVOKABLE void refreshTimeline() {}
    Q_INVOKABLE void openModelsFolder() {}
    Q_INVOKABLE void completeFirstRun() {}
    Q_INVOKABLE bool addModel(const QString&, const QString&) { return true; }
    Q_INVOKABLE bool pickAndAddModel(const QString&) { return true; }
    Q_INVOKABLE void setModelRole(const QString&, const QString&) {}
    Q_INVOKABLE QVariantList listMemories(const QString& = {}, int = 100) const {
        return {
            QVariantMap{{"id", 1}, {"kind", "note"},
                        {"text", "Owner prefers af_heart TTS voice."},
                        {"source", "ui"}, {"userId", -1}, {"ts", 1720000000}},
            QVariantMap{{"id", 2}, {"kind", "fact"},
                        {"text", "Polymath runs on RTX 2070 Max-Q 8 GB."},
                        {"source", "agent"}, {"userId", -1}, {"ts", 1720001000}},
        };
    }
    Q_INVOKABLE bool deleteMemory(qint64) { return true; }
    Q_INVOKABLE bool rememberNote(const QString&, const QString& = {}) { return true; }
    Q_INVOKABLE qint64 activeUserId() const { return -1; }
    Q_INVOKABLE void setActiveUserId(qint64) {}
    Q_INVOKABLE QVariantList listUsers() const { return {}; }
    Q_INVOKABLE void spawnSurfaceDemo() {}

    // E2 personality write-API pass-throughs (report success; no-op).
    Q_INVOKABLE bool createPersonality(const QString&) { return true; }
    Q_INVOKABLE bool savePersonality(const QString&, const QString&) { return true; }
    Q_INVOKABLE bool setPersonalityAvatar(const QString&, const QString&) { return true; }
    Q_INVOKABLE bool deletePersonality(const QString&) { return true; }
    Q_INVOKABLE QStringList availableToolNames() const {
        return {QStringLiteral("web_search"), QStringLiteral("fetch_page"),
                QStringLiteral("remember"), QStringLiteral("recall"),
                QStringLiteral("shopping_add"), QStringLiteral("reminder_set"),
                QStringLiteral("ui_control"), QStringLiteral("youtube_search"),
                QStringLiteral("screen_capture"), QStringLiteral("fs_read"),
                QStringLiteral("fs_write"), QStringLiteral("run_command")};
    }
    Q_INVOKABLE QStringList availableModels() const {
        return {QStringLiteral("fast"), QStringLiteral("heavy"),
                QStringLiteral("gemma-3n-E4B-it-Q4_K_M"),
                QStringLiteral("gemma-3-27b-it-Q4_K_M")};
    }

    // C1 confirm relay (mirror AppController).
    Q_INVOKABLE void respondConfirm(const QString&, bool, bool = false) {}
signals:
    void changed();
    void modelsChanged();
    void firstRunChanged();
    void assistantToken(QString, QString, bool);
    void noticePosted(QString, QString, QString);
    void findObjectAnswered(QString, QString);
    void vramChanged();
    void wakeWordPulse();
    // A3 extended surfaceRequested (12 params; QML Connections may bind fewer).
    void surfaceRequested(QString, QString, QString, QString, QString,
                          QString, QString, double, double, double, double, QString);
    void goalUpdated(QString, QString, QString, QString);
    // A3 / E4: page nav + window verbs.
    void navigateRequested(QString);
    void windowRequested(QString);
    // C1 confirm dialog relay.
    void confirmRequested(QString, QString, QString, QString, QString);
    void confirmSettled(QString);
private:
    QObject* chat_ = nullptr;
};

// ---------------------------------------------------------------------------
// Stub `settings` — SettingsController surface for Style bridge / SettingsView.
// ---------------------------------------------------------------------------
class StubSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(bool effects READ effects WRITE setEffects NOTIFY effectsChanged)
    Q_PROPERTY(double effectsIntensity READ effectsIntensity WRITE setEffectsIntensity
               NOTIFY effectsIntensityChanged)
    Q_PROPERTY(double fontScale READ fontScale WRITE setFontScale NOTIFY fontScaleChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion WRITE setReduceMotion NOTIFY reduceMotionChanged)
    // D4 Voice section (typed properties — SettingsView binds them directly).
    Q_PROPERTY(QString ttsEngine READ ttsEngine WRITE setTtsEngine NOTIFY ttsEngineChanged)
    Q_PROPERTY(QString ttsVoice  READ ttsVoice  WRITE setTtsVoice  NOTIFY ttsVoiceChanged)
    Q_PROPERTY(double  ttsSpeed  READ ttsSpeed  WRITE setTtsSpeed  NOTIFY ttsSpeedChanged)
    Q_PROPERTY(double  ttsVolume READ ttsVolume WRITE setTtsVolume NOTIFY ttsVolumeChanged)
public:
    QString accent() const { return accent_; }
    bool effects() const { return effects_; }
    double effectsIntensity() const { return intensity_; }
    double fontScale() const { return scale_; }
    bool reduceMotion() const { return reduce_; }
    QString ttsEngine() const { return tts_engine_; }
    QString ttsVoice() const { return tts_voice_; }
    double  ttsSpeed() const { return tts_speed_; }
    double  ttsVolume() const { return tts_volume_; }
    void setAccent(const QString& v) { if (accent_ == v) return; accent_ = v; emit accentChanged(); emit settingChanged("ui.accent", v); }
    void setEffects(bool v) { if (effects_ == v) return; effects_ = v; emit effectsChanged(); emit settingChanged("ui.effects", v); }
    void setEffectsIntensity(double v) { if (intensity_ == v) return; intensity_ = v; emit effectsIntensityChanged(); }
    void setFontScale(double v) { if (scale_ == v) return; scale_ = v; emit fontScaleChanged(); }
    void setReduceMotion(bool v) { if (reduce_ == v) return; reduce_ = v; emit reduceMotionChanged(); }
    void setTtsEngine(const QString& v) {
        if (tts_engine_ == v) return; tts_engine_ = v;
        emit ttsEngineChanged(); emit settingChanged("tts.engine", v);
    }
    void setTtsVoice(const QString& v) {
        if (tts_voice_ == v) return; tts_voice_ = v;
        emit ttsVoiceChanged(); emit settingChanged("tts.voice", v);
    }
    void setTtsSpeed(double v) { if (tts_speed_ == v) return; tts_speed_ = v; emit ttsSpeedChanged(); }
    void setTtsVolume(double v) { if (tts_volume_ == v) return; tts_volume_ = v; emit ttsVolumeChanged(); }

    // C1 Safety keys return sensible defaults when callers omit / use empty def.
    Q_INVOKABLE QString getString(const QString& key, const QString& def = {}) const {
        if (key == QLatin1String("safety.mode") && def.isEmpty())
            return QStringLiteral("standard");
        if (key == QLatin1String("safety.fs_allowed_roots") && def.isEmpty())
            return QStringLiteral("Documents;Desktop;Downloads;@data");
        if (key == QLatin1String("safety.cmd_denylist") && def.isEmpty())
            return QStringLiteral("rm -rf;format;mkfs;del /f");
        if (key == QLatin1String("safety.tool_overrides") && def.isEmpty())
            return QStringLiteral("run_command");
        return def;
    }
    Q_INVOKABLE int getInt(const QString&, int def = 0) const { return def; }
    Q_INVOKABLE bool getBool(const QString& key, bool def = false) const {
        if (key == QLatin1String("safety.audit")) return true;
        return def;
    }
    Q_INVOKABLE double getReal(const QString&, double def = 0.0) const { return def; }
    Q_INVOKABLE void setString(const QString& k, const QString& v) { emit settingChanged(k, v); }
    Q_INVOKABLE void setInt(const QString& k, int v) { emit settingChanged(k, v); }
    Q_INVOKABLE void setBool(const QString& k, bool v) { emit settingChanged(k, v); }
    Q_INVOKABLE void setReal(const QString& k, double v) { emit settingChanged(k, v); }
    Q_INVOKABLE QVariantList audioInputDevices() const {
        return {QVariantMap{{"id",""},{"label","System default"}},
                QVariantMap{{"id","mic0"},{"label","Microphone (Realtek)"}}};
    }
    Q_INVOKABLE QVariantList audioOutputDevices() const {
        return {QVariantMap{{"id",""},{"label","System default"}},
                QVariantMap{{"id","spk0"},{"label","Speakers"}}};
    }
    // D4: populated voice combo for SettingsView + PersonalityEditor.
    Q_INVOKABLE QVariantList ttsVoices() const {
        return {
            QVariantMap{{"id","af_heart"},{"label","Heart (US female, warm)"}},
            QVariantMap{{"id","am_adam"},{"label","Adam (US male)"}},
            QVariantMap{{"id","bf_emma"},{"label","Emma (UK female)"}},
        };
    }
    Q_INVOKABLE void previewVoice(const QString& = QString()) {}
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
    void settingChanged(QString, QVariant);
private:
    QString accent_ = QStringLiteral("#33E1FF");
    bool effects_ = true;
    double intensity_ = 0.6;
    double scale_ = 1.0;
    bool reduce_ = false;
    QString tts_engine_ = QStringLiteral("auto");
    QString tts_voice_  = QStringLiteral("af_heart");
    double  tts_speed_  = 1.0;
    double  tts_volume_ = 1.0;
};

// ---------------------------------------------------------------------------
// Stub `notifications` — NotificationsModel surface.
// ---------------------------------------------------------------------------
class StubNotifications : public StubListModel {
    Q_OBJECT
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)
public:
    StubNotifications(QVariantList rows, QObject* parent = nullptr)
        : StubListModel(
              {"id","severity","source","title","body","timestamp","timeLabel",
               "read","category","pendingAction","confirmId"},
              std::move(rows), parent) {}
    int unreadCount() const {
        return unread_;
    }
    void setUnread(int n) { unread_ = n; emit unreadCountChanged(); }
    Q_INVOKABLE void markAllRead() { unread_ = 0; emit unreadCountChanged(); }
    Q_INVOKABLE void markRead(const QString&) {}
    Q_INVOKABLE void clearAll() {}
    Q_INVOKABLE void refreshFromEvents() {}
    // C1 confirm inline actions (NotificationCenter chrome may use later).
    Q_INVOKABLE void approveConfirm(const QString&) {}
    Q_INVOKABLE void denyConfirm(const QString&) {}
signals:
    void unreadCountChanged();
private:
    int unread_ = 0;
};

// ---------------------------------------------------------------------------
// Stub `gateway` — the GatewayService surface MobileAccessView binds to, seeded
// with a realistic single-use pairing payload so the QR encoder + payload box
// render populated (and scannable) in the capture.
// ---------------------------------------------------------------------------
class StubGateway : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE QString pairingPayloadJson() const {
        return QStringLiteral(
            "{\"relay_url\":\"\",\"home_id\":\"5b80f020-571e-4293-a5ae-0e19c9b814c9\","
            "\"pair_code\":\"482915\",\"lan_host\":\"polymath.local\",\"lan_port\":8765}");
    }
    Q_INVOKABLE QString pairingDeepLink() const {
        return QStringLiteral("polymath://pair?home_id=5b80f020-571e-4293-a5ae-0e19c9b814c9"
                              "&code=482915&host=polymath.local&port=8765");
    }
    Q_INVOKABLE bool remoteEnabled() const { return false; }
    Q_INVOKABLE void setRemoteEnabled(bool) {}
    Q_INVOKABLE int  connectedClients() const { return 1; }
signals:
    void connectedClientsChanged(int);
    void remoteEnabledChanged(bool);
};

// Spin the event loop briefly so async Image/Loader/Canvas work settles.
static void settle(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Grab a window's framebuffer to PNG (offscreen-safe).
static bool grab(QQuickWindow* win, const QString& path) {
    // Let the scene graph initialise and any async Image/Loader/Canvas settle.
    for (int i = 0; i < 4 && !win->isSceneGraphInitialized(); ++i) settle(120);
    settle(400);
    QImage img = win->grabWindow();
    if (img.isNull()) {
        fprintf(stderr, "  grabWindow() returned null for %s\n", qPrintable(path));
        return false;
    }
    const bool ok = img.save(path, "PNG");
    if (!ok) fprintf(stderr, "  save failed for %s\n", qPrintable(path));
    return ok;
}

// B2 seed: VideoPickerSurface argsJson with 6 fake youtube_search-shaped results.
// thumbnailUrl left blank so offline capture hits the deterministic "No thumbnail" path.
static const char* kVideoPickerSeedJs =
    "item.title = 'Watch: castles';\n"
    "item.argsJson = JSON.stringify({results:["
    "{videoId:'dQw4w9WgXcQ',title:'Castles of Scotland — a walking tour through 900 years of history',"
    "channel:'Heritage Explorer',durationSec:754,views:'1.2M views',"
    "publishedText:'3 years ago',thumbnailUrl:'',watchUrl:'https://www.youtube.com/watch?v=dQw4w9WgXcQ'},"
    "{videoId:'aBcDeFgHiJk',title:'Neuschwanstein Castle drone footage 4K',"
    "channel:'Skyline Drones',durationSec:312,views:'845K views',"
    "publishedText:'1 year ago',thumbnailUrl:'',watchUrl:'https://www.youtube.com/watch?v=aBcDeFgHiJk'},"
    "{videoId:'kLmNoPqRsTu',title:'How medieval castles actually defended against siege engines',"
    "channel:'History Lab',durationSec:1123,views:'3.4M views',"
    "publishedText:'8 months ago',thumbnailUrl:'',watchUrl:'https://www.youtube.com/watch?v=kLmNoPqRsTu'},"
    "{videoId:'vWxYzAbCdEf',title:'Edinburgh Castle full tour (no talking, ambient only)',"
    "channel:'Wander Quiet',durationSec:2640,views:'210K views',"
    "publishedText:'2 weeks ago',thumbnailUrl:'',watchUrl:'https://www.youtube.com/watch?v=vWxYzAbCdEf'},"
    "{videoId:'gHiJkLmNoPq',title:'Top 10 fairytale castles in Europe you can actually visit',"
    "channel:'Offbeat Travel',durationSec:611,views:'5.7M views',"
    "publishedText:'5 years ago',thumbnailUrl:'',watchUrl:'https://www.youtube.com/watch?v=gHiJkLmNoPq'},"
    "{videoId:'rStUvWxYzAb',title:'Building a castle with hand tools only — 3 year timelapse',"
    "channel:'Guédelon Project',durationSec:5431,views:'18M views',"
    "publishedText:'10 months ago',thumbnailUrl:'',watchUrl:'https://www.youtube.com/watch?v=rStUvWxYzAb'}"
    "]});\n";

// E3 seed: NoteSurface markdown body + title.
static const char* kNoteSeedJs =
    "item.title = 'Castle research';\n"
    "item.md = '# Neuschwanstein\\n\\nBuilt for **Ludwig II** starting 1869; "
    "never fully completed. Known for its fairy-tale silhouette "
    "and heavy Wagner influence.\\n\\n- Book tickets online\\n"
    "- Bring good shoes\\n- [Official site](https://example.org)';\n";

// E3 seed: ImageSurface caption bar + empty source (deterministic placeholder).
static const char* kImageSeedJs =
    "item.title = 'Exterior';\n"
    "item.caption = 'West façade, autumn 2019';\n";

// E3 board layout demo — full custom Window QML (calls SurfaceHost.spawn directly).
// Uses video_picker (not WebEngine "video") so the harness stays headless-safe.
static const char* kBoardDemoQml =
    "import QtQuick\n"
    "import Polymath\n"
    "Window {\n"
    "  width: 1280; height: 820; visible: true; color: Style.bg\n"
    "  Component.onCompleted: Style.effectsEnabled = pmEffectsEnabled\n"
    "  SurfaceHost {\n"
    "    id: host\n"
    "    anchors.fill: parent\n"
    "    Component.onCompleted: {\n"
    "      host.spawn('n1', 'note', 'Castle research',\n"
    "        JSON.stringify({}), '',\n"
    "        '# Neuschwanstein\\n\\nBuilt for **Ludwig II** starting 1869.\\n\\n'\n"
    "        + '- Fairy-tale silhouette\\n- Heavy Wagner influence',\n"
    "        -1, -1, -1, -1, 'Castles')\n"
    "      host.spawn('i1', 'image', 'Exterior',\n"
    "        JSON.stringify({}), 'West façade, autumn 2019', '',\n"
    "        -1, -1, -1, -1, 'Castles')\n"
    "      host.spawn('n2', 'note', 'Trip notes',\n"
    "        JSON.stringify({}), '',\n"
    "        '- Book tickets online\\n- Bring good shoes\\n- Check opening hours',\n"
    "        -1, -1, -1, -1, 'Trip planning')\n"
    "      host.spawn('i2', 'image', 'Approach map',\n"
    "        JSON.stringify({}), 'Approach trail from the car park', '',\n"
    "        -1, -1, -1, -1, 'Trip planning')\n"
    "      host.spawn('v1', 'video_picker', 'Search: castle drone footage',\n"
    "        JSON.stringify({results: [\n"
    "          {videoId:'aBcDeFgHiJk', title:'Neuschwanstein Castle drone footage 4K',\n"
    "           channel:'Skyline Drones', durationSec:312, views:'845K views',\n"
    "           publishedText:'1 year ago', thumbnailUrl:'', watchUrl:''}\n"
    "        ]}), '', '', -1, -1, -1, -1, 'Trip planning')\n"
    "      host.arrange('board')\n"
    "    }\n"
    "  }\n"
    "}\n";

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);  // deterministic, GPU-free
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");

    const QString outDir = argc > 1 ? argv[1] : ".";
    bool empty = false;
    for (int i = 2; i < argc; ++i) if (QString(argv[i]) == "--empty") empty = true;
    QDir().mkpath(outDir);

    // Bundle font app-wide, exactly as Main.qml's FontLoader does at runtime.
    QFontDatabase::addApplicationFont(":/qt/qml/Polymath/fonts/Inter.ttf");
    app.setFont(QFont("Inter", 10));

    // --- shared stub context ------------------------------------------------
    StubApp stub;
    stub.populated = !empty;
    StubGateway stubGw;   // seeds MobileAccessView's pairing QR + payload

    auto chat = empty ? new StubListModel({"who","text","streaming","requestId"}, {}, &stub)
        : new StubListModel({"who","text","streaming","requestId"}, QVariantList{
            QVariantMap{{"who","you"},{"text","Where did I leave my keys?"},{"streaming",false}},
            QVariantMap{{"who","assistant"},{"text","You set them on the kitchen counter at 4:12 PM — the camera caught it."},{"streaming",false}},
            QVariantMap{{"who","you"},{"text","Add milk to the shopping list."},{"streaming",false}},
            QVariantMap{{"who","assistant"},{"text","Done. Milk is on your list."},{"streaming",true}},
        }, &stub);
    stub.setChat(chat);

    auto shopping = empty ? new StubListModel({"itemId","item","quantity","done","createdAt"}, {}, &stub)
        : new StubListModel({"itemId","item","quantity","done","createdAt"}, QVariantList{
            QVariantMap{{"item","Milk"},{"quantity","2 L"},{"done",false}},
            QVariantMap{{"item","Coffee beans"},{"quantity","1 kg"},{"done",false}},
            QVariantMap{{"item","Eggs"},{"quantity","dozen"},{"done",true}},
            QVariantMap{{"item","Olive oil"},{"quantity",""},{"done",false}},
        }, &stub);

    auto cameras = empty ? new StubListModel({"cameraId","name","location","enabled","live","frameTick"}, {}, &stub)
        : new StubListModel({"cameraId","name","location","enabled","live","frameTick"}, QVariantList{
            QVariantMap{{"cameraId",1},{"name","Front Door"},{"location","entry"},{"enabled",true},{"live",true},{"frameTick",3}},
            QVariantMap{{"cameraId",2},{"name","Kitchen"},{"location","ground floor"},{"enabled",true},{"live",true},{"frameTick",5}},
            QVariantMap{{"cameraId",3},{"name","Garage"},{"location","side"},{"enabled",false},{"live",false},{"frameTick",0}},
        }, &stub);

    auto tasks = empty ? new StubListModel({"taskId","type","status","detail","priority"}, {}, &stub)
        : new StubListModel({"taskId","type","status","detail","priority"}, QVariantList{
            QVariantMap{{"type","Daily summary"},{"status","running"},{"detail","Summarising today's transcripts and events"},{"priority",2}},
            QVariantMap{{"type","Lab report"},{"status","queued"},{"detail","Synthesise the weekly sensor analysis"},{"priority",1}},
            QVariantMap{{"type","Research"},{"status","done"},{"detail","Compared three espresso grinders"},{"priority",0}},
            QVariantMap{{"type","Web fetch"},{"status","error"},{"detail","Source timed out after 3 retries"},{"priority",0}},
        }, &stub);

    // D1: ScheduledGoalsModel for TaskQueueView "Scheduled" section.
    auto schedules = empty
        ? new StubListModel(
              {"scheduleId","title","kind","spec","nextFire","lastFire","enabled",
               "deliver","skill","prompt"}, {}, &stub)
        : new StubListModel(
              {"scheduleId","title","kind","spec","nextFire","lastFire","enabled",
               "deliver","skill","prompt"},
              QVariantList{
                  QVariantMap{{"scheduleId",1},{"title","Morning briefing"},
                              {"kind","rrule"},{"spec","FREQ=DAILY"},
                              {"nextFire",2000000000},{"lastFire",0},
                              {"enabled",true},{"deliver","voice"},
                              {"skill",""},{"prompt","Give me my morning briefing"}},
                  QVariantMap{{"scheduleId",2},{"title","Check the oven timer"},
                              {"kind","at"},{"spec","1999999999"},
                              {"nextFire",1999999999},{"lastFire",0},
                              {"enabled",true},{"deliver","chat"},
                              {"skill",""},{"prompt","Remind me to check the oven"}},
                  QVariantMap{{"scheduleId",3},{"title","Weekly session digest"},
                              {"kind","every"},{"spec","604800"},
                              {"nextFire",2000100000},{"lastFire",1999500000},
                              {"enabled",false},{"deliver","notify"},
                              {"skill","session_digest"},{"prompt",""}},
              }, &stub);

    auto timeline = empty ? new StubListModel({"category","kind","text","timestamp","timeLabel"}, {}, &stub)
        : new StubListModel({"category","kind","text","timestamp","timeLabel"}, QVariantList{
            QVariantMap{{"category","event"},{"kind","person"},{"text","Person detected at Front Door"},{"timeLabel","16:12"}},
            QVariantMap{{"category","transcript"},{"kind","command"},{"text","\"Add milk to the shopping list\""},{"timeLabel","16:10"}},
            QVariantMap{{"category","memory"},{"kind","summary"},{"text","Quiet afternoon; two deliveries; coffee restocked."},{"timeLabel","15:40"}},
            QVariantMap{{"category","event"},{"kind","motion"},{"text","Motion in Kitchen"},{"timeLabel","15:31"}},
            QVariantMap{{"category","transcript"},{"kind","ambient"},{"text","\"...let's plan the trip this weekend...\""},{"timeLabel","14:58"}},
        }, &stub);

    // E2: PersonalityModel for PersonalitiesView + PersonalityEditor.
    auto personalities = empty
        ? new StubListModel(
              {"name","systemPrompt","voice","preferredModel","wakePhrase","tools",
               "temperature","topP","topK","repeatPenalty","maxTokens","avatarPath","isActive"},
              {}, &stub)
        : new StubListModel(
              {"name","systemPrompt","voice","preferredModel","wakePhrase","tools",
               "temperature","topP","topK","repeatPenalty","maxTokens","avatarPath","isActive"},
              QVariantList{
                  QVariantMap{{"name","Assistant"},
                              {"systemPrompt","You are a helpful local home assistant."},
                              {"voice",""},{"preferredModel","fast"},{"wakePhrase",""},
                              {"tools",QStringList{}},
                              {"temperature",0.7},{"topP",0.9},{"topK",40},
                              {"repeatPenalty",1.1},{"maxTokens",1024},
                              {"avatarPath",""},{"isActive",false}},
                  QVariantMap{{"name","Marcus Aurelius"},
                              {"systemPrompt","You are Marcus Aurelius, Roman emperor and Stoic philosopher. You speak with calm, measured wisdom..."},
                              {"voice","en_GB-alan-medium"},{"preferredModel","fast"},{"wakePhrase","Marcus"},
                              {"tools",QStringList{}},
                              {"temperature",0.7},{"topP",0.9},{"topK",40},
                              {"repeatPenalty",1.1},{"maxTokens",1024},
                              {"avatarPath",""},{"isActive",true}},
                  QVariantMap{{"name","Ada Lovelace"},
                              {"systemPrompt","You are Ada Lovelace, mathematician and writer, the first computer programmer..."},
                              {"voice","en_GB-jenny_dioco-medium"},{"preferredModel","fast"},{"wakePhrase","Ada"},
                              {"tools",QStringList{}},
                              {"temperature",0.75},{"topP",0.9},{"topK",40},
                              {"repeatPenalty",1.1},{"maxTokens",1024},
                              {"avatarPath",""},{"isActive",false}},
              }, &stub);

    StubSettings stubSettings;
    auto notifRows = empty ? QVariantList{} : QVariantList{
        QVariantMap{{"id","n1"},{"severity","info"},{"source","system"},{"title","Welcome"},
                    {"body","Polymath is online."},{"timestamp",0},{"timeLabel","16:00"},
                    {"read",false},{"category","notice"},
                    {"pendingAction",false},{"confirmId",""}},
        QVariantMap{{"id","n2"},{"severity","good"},{"source","tasks"},{"title","Lab report"},
                    {"body","done — weekly synthesis ready"},{"timestamp",0},{"timeLabel","15:40"},
                    {"read",false},{"category","task"},
                    {"pendingAction",false},{"confirmId",""}},
        QVariantMap{{"id","n3"},{"severity","warn"},{"source","reminders"},{"title","Reminder"},
                    {"body","Take out the recycling"},{"timestamp",0},{"timeLabel","14:00"},
                    {"read",true},{"category","reminder"},
                    {"pendingAction",false},{"confirmId",""}},
        // C1: pending confirm row for NotificationCenter populated capture.
        QVariantMap{{"id","cap-confirm-1"},{"severity","warn"},{"source","safety"},
                    {"title","Needs approval: fs_write"},
                    {"body","fs_write: C:/Users/…/Documents/notes.txt"},
                    {"timestamp",0},{"timeLabel","12:00"},
                    {"read",false},{"category","confirm"},
                    {"pendingAction",true},{"confirmId","cap-confirm-1"}},
    };
    auto* notifications = new StubNotifications(notifRows, &stub);
    notifications->setUnread(empty ? 0 : 3);

    // C4: agentSessions model for AgentSessionsView (+ Dashboard agent count).
    auto agentSessions = empty
        ? new StubListModel(
              {"sessionId","provider","title","cwd","status","lastMessage",
               "costUsd","elapsed","unreadPing","experimental","createdAt","updatedAt"},
              {}, &stub)
        : new StubListModel(
              {"sessionId","provider","title","cwd","status","lastMessage",
               "costUsd","elapsed","unreadPing","experimental","createdAt","updatedAt"},
              QVariantList{
                  QVariantMap{{"sessionId","a1"},{"provider","claude-code"},
                              {"title","Refactor auth"},{"cwd","C:/work/app"},
                              {"status","needs_input"},
                              {"lastMessage","Allow editing src/auth/token.cpp?"},
                              {"costUsd",0.042},{"elapsed","3m"},
                              {"unreadPing",true},{"experimental",false}},
                  QVariantMap{{"sessionId","a2"},{"provider","claude-code"},
                              {"title","Say READY"},{"cwd","C:/tmp"},
                              {"status","working"},
                              {"lastMessage","Running tests…"},
                              {"costUsd",0.01},{"elapsed","45s"},
                              {"unreadPing",false},{"experimental",false}},
                  QVariantMap{{"sessionId","a3"},{"provider","codex"},
                              {"title","Explore repo"},{"cwd","C:/work/app"},
                              {"status","done"},
                              {"lastMessage","Summary written to NOTES.md"},
                              {"costUsd",0.12},{"elapsed","12m"},
                              {"unreadPing",false},{"experimental",true}},
              }, &stub);

    // --- helper: render one QML file to PNG ---------------------------------
    // extraOnLoaded: JS run when Loader is Ready (surface seeds).
    // customWindowQml: when non-empty, use as full Window QML (board demo).
    auto renderView = [&](const QString& qmlUrl, const QString& outPng, bool isWindow,
                          const QString& extraOnLoaded = {},
                          const QString& customWindowQml = {}) -> bool {
        QQmlApplicationEngine engine;
        QQmlContext* ctx = engine.rootContext();
        ctx->setContextProperty("app", &stub);
        ctx->setContextProperty("chatModel", chat);
        ctx->setContextProperty("shoppingModel", shopping);
        ctx->setContextProperty("cameraModel", cameras);
        ctx->setContextProperty("taskModel", tasks);
        ctx->setContextProperty("scheduledGoalsModel", schedules);
        ctx->setContextProperty("timelineModel", timeline);
        ctx->setContextProperty("personalityModel", personalities);
        // MobileAccessView reads a `gateway` context property at runtime; in the
        // headless harness we feed a stub seeded with a sample pairing payload so
        // the QR encoder + payload box render populated (and scannable).
        ctx->setContextProperty("gateway", &stubGw);
        ctx->setContextProperty("settings", &stubSettings);
        ctx->setContextProperty("notifications", notifications);
        ctx->setContextProperty("agentSessions", agentSessions);
        // Software capture path: force faux-glass (no MultiEffect blur).
        ctx->setContextProperty("pmEffectsEnabled", false);

        // E3 board demo (or any full custom Window string).
        if (!customWindowQml.isEmpty()) {
            QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                             [](const QList<QQmlError>& warnings) {
                                 for (const auto& w : warnings)
                                     fprintf(stderr, "  QML: %s\n", qPrintable(w.toString()));
                             });
            QQmlComponent comp(&engine);
            comp.setData(customWindowQml.toUtf8(), QUrl("qrc:/wrapper-custom.qml"));
            QObject* obj = comp.create(ctx);
            if (!obj) {
                fprintf(stderr, "  custom window failed: %s\n",
                        qPrintable(comp.errorString()));
                return false;
            }
            auto* win = qobject_cast<QQuickWindow*>(obj);
            if (!win) { fprintf(stderr, "  custom not a window\n"); delete obj; return false; }
            win->show();
            // Board arrange needs a beat after spawn for loaders/layout.
            settle(200);
            const bool ok = grab(win, outPng);
            win->close();
            delete obj;
            return ok;
        }

        if (isWindow) {
            // Main.qml is an ApplicationWindow — load it and grab the window.
            QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                             [](const QList<QQmlError>& warnings) {
                                 for (const auto& w : warnings)
                                     fprintf(stderr, "  QML: %s\n", qPrintable(w.toString()));
                             });
            engine.load(QUrl(qmlUrl));
            if (engine.rootObjects().isEmpty()) {
                fprintf(stderr, "  load failed %s\n", qPrintable(qmlUrl)); return false;
            }
            auto* win = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            if (!win) { fprintf(stderr, "  not a window %s\n", qPrintable(qmlUrl)); return false; }
            win->resize(1280, 820);
            win->show();
            const bool ok = grab(win, outPng);
            win->close();
            return ok;
        }

        // A plain Item view — wrap it in a sized window so it has a surface.
        // Bridge pmEffectsEnabled → Style so glass stays faux under Software.
        // Optional extraOnLoaded seeds surface props (title/argsJson/md/…).
        const QString onReady = extraOnLoaded.isEmpty()
            ? QStringLiteral("if (status === Loader.Error) console.error('loader error')")
            : QStringLiteral(
                  "if (status === Loader.Ready) { %1 } "
                  "else if (status === Loader.Error) console.error('loader error')")
                  .arg(extraOnLoaded);
        const QString wrapper =
            "import QtQuick\n"
            "import Polymath\n"
            "Window {\n"
            "  width: 1040; height: 760; visible: true; color: Style.bg\n"
            "  Component.onCompleted: Style.effectsEnabled = pmEffectsEnabled\n"
            "  Loader { anchors.fill: parent; source: \"" + qmlUrl + "\";\n"
            "    onStatusChanged: " + onReady + " }\n"
            "}\n";
        QQmlComponent comp(&engine);
        comp.setData(wrapper.toUtf8(), QUrl("qrc:/wrapper.qml"));
        QObject* obj = comp.create(ctx);
        if (!obj) {
            fprintf(stderr, "  wrapper failed %s : %s\n",
                    qPrintable(qmlUrl), qPrintable(comp.errorString()));
            return false;
        }
        auto* win = qobject_cast<QQuickWindow*>(obj);
        if (!win) { fprintf(stderr, "  wrapper not a window\n"); return false; }
        win->show();
        const bool ok = grab(win, outPng);
        win->close();
        delete obj;
        return ok;
    };

    // file: QML under qml/ (or surfaces/…); empty for customWindow-only entries.
    // extraOnLoaded / customWindow: optional seed paths (B2/E3 surfaces).
    struct View {
        const char* file;
        const char* png;
        bool window;
        const char* extraOnLoaded = "";
        const char* customWindow = nullptr;
    };
    const std::vector<View> views = {
        {"Main.qml",              "01-main-shell",   true },
        {"Dashboard.qml",         "02-dashboard",    false},
        {"ChatView.qml",          "03-chat",         false},
        {"CamerasView.qml",       "04-cameras",      false},
        {"TaskQueueView.qml",     "05-tasks",        false},
        {"TimelineView.qml",      "06-timeline",     false},
        {"MemoryView.qml",        "06b-memories",    false},
        {"ShoppingView.qml",      "07-shopping",     false},
        {"PersonalitiesView.qml", "08-personalities",false},
        {"PersonalityEditor.qml", "08b-personality-editor", false},  // E2 create-new state
        {"ModelManagerView.qml",  "09-models",       false},
        {"PrivacyView.qml",       "10-privacy",      false},
        {"MobileAccessView.qml",  "11-mobile-access",false},
        {"SettingsView.qml",      "12-settings",     false},
        {"AgentSessionsView.qml", "13-agents",       false},
        // B2 / E3 standalone surfaces (seeded via extraOnLoaded).
        {"surfaces/VideoPickerSurface.qml", "14-video-picker", false, kVideoPickerSeedJs},
        {"surfaces/NoteSurface.qml",        "15-note-surface", false, kNoteSeedJs},
        {"surfaces/ImageSurface.qml",       "16-image-surface", false, kImageSeedJs},
        {"surfaces/PlaceholderSurface.qml", "17-placeholder-surface", false},
        // E3 board layout demo (custom Window + SurfaceHost.spawn).
        {nullptr, "18-surface-board", false, "", kBoardDemoQml},
    };

    int failures = 0;
    const QString suffix = empty ? "-empty" : "";
    for (const auto& v : views) {
        const QString url = v.file
            ? (QStringLiteral("qrc:/qt/qml/Polymath/qml/") + v.file)
            : QString();
        const QString out = outDir + "/" + v.png + suffix + ".png";
        const QString extra = v.extraOnLoaded ? QString::fromUtf8(v.extraOnLoaded) : QString();
        const QString custom = v.customWindow ? QString::fromUtf8(v.customWindow) : QString();
        const bool ok = renderView(url, out, v.window, extra, custom);
        fprintf(stderr, "%s  %s\n", ok ? "OK  " : "FAIL", qPrintable(out));
        if (!ok) ++failures;
    }
    fprintf(stderr, "captured %d / %d views%s\n",
            int(views.size()) - failures, int(views.size()),
            empty ? " (empty/first-run states)" : "");
    fflush(stderr);
    return failures == 0 ? 0 : 3;
}

#include "capture_views.moc"
