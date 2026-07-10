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
    // Dashboard HUD: count rows whose "status" role matches (case-sensitive).
    Q_INVOKABLE int countByStatus(const QString& status) const {
        int n = 0;
        for (const auto& row : rows_) {
            if (row.toMap().value(QStringLiteral("status")).toString() == status)
                ++n;
        }
        return n;
    }

private:
    QHash<int, QByteArray> roles_;
    QVariantList rows_;
    Q_OBJECT
    Q_PROPERTY(QString filter MEMBER filter_)
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
    Q_INVOKABLE void refreshTimeline() {}
    Q_INVOKABLE void openModelsFolder() {}
    Q_INVOKABLE void completeFirstRun() {}
    Q_INVOKABLE bool addModel(const QString&, const QString&) { return true; }
    Q_INVOKABLE void setModelRole(const QString&, const QString&) {}
    Q_INVOKABLE void spawnSurfaceDemo() {}
signals:
    void changed();
    void modelsChanged();
    void firstRunChanged();
    void assistantToken(QString, QString, bool);
    void noticePosted(QString, QString, QString);
    void findObjectAnswered(QString, QString);
    void vramChanged();
    void wakeWordPulse();
    void surfaceRequested(QString, QString, QString, QString, QString);
    void goalUpdated(QString, QString, QString, QString);
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
public:
    QString accent() const { return accent_; }
    bool effects() const { return effects_; }
    double effectsIntensity() const { return intensity_; }
    double fontScale() const { return scale_; }
    bool reduceMotion() const { return reduce_; }
    void setAccent(const QString& v) { if (accent_ == v) return; accent_ = v; emit accentChanged(); emit settingChanged("ui.accent", v); }
    void setEffects(bool v) { if (effects_ == v) return; effects_ = v; emit effectsChanged(); emit settingChanged("ui.effects", v); }
    void setEffectsIntensity(double v) { if (intensity_ == v) return; intensity_ = v; emit effectsIntensityChanged(); }
    void setFontScale(double v) { if (scale_ == v) return; scale_ = v; emit fontScaleChanged(); }
    void setReduceMotion(bool v) { if (reduce_ == v) return; reduce_ = v; emit reduceMotionChanged(); }

    Q_INVOKABLE QString getString(const QString&, const QString& def = {}) const { return def; }
    Q_INVOKABLE int getInt(const QString&, int def = 0) const { return def; }
    Q_INVOKABLE bool getBool(const QString&, bool def = false) const { return def; }
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
signals:
    void accentChanged();
    void effectsChanged();
    void effectsIntensityChanged();
    void fontScaleChanged();
    void reduceMotionChanged();
    void settingChanged(QString, QVariant);
private:
    QString accent_ = QStringLiteral("#33E1FF");
    bool effects_ = true;
    double intensity_ = 0.6;
    double scale_ = 1.0;
    bool reduce_ = false;
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
              {"id","severity","source","title","body","timestamp","timeLabel","read","category"},
              std::move(rows), parent) {}
    int unreadCount() const {
        int n = 0;
        // rows_ is private in base — approximate from seeded data via data() API.
        for (int i = 0; i < rowCount(); ++i) {
            if (!data(index(i, 0), Qt::UserRole + 8).toBool()) // read role
                ++n;
        }
        // Role mapping: Id=UserRole+1 ... Read=UserRole+8. Safer: store count.
        return unread_;
    }
    void setUnread(int n) { unread_ = n; emit unreadCountChanged(); }
    Q_INVOKABLE void markAllRead() { unread_ = 0; emit unreadCountChanged(); }
    Q_INVOKABLE void markRead(const QString&) {}
    Q_INVOKABLE void clearAll() {}
    Q_INVOKABLE void refreshFromEvents() {}
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

    auto timeline = empty ? new StubListModel({"category","kind","text","timestamp","timeLabel"}, {}, &stub)
        : new StubListModel({"category","kind","text","timestamp","timeLabel"}, QVariantList{
            QVariantMap{{"category","event"},{"kind","person"},{"text","Person detected at Front Door"},{"timeLabel","16:12"}},
            QVariantMap{{"category","transcript"},{"kind","command"},{"text","\"Add milk to the shopping list\""},{"timeLabel","16:10"}},
            QVariantMap{{"category","memory"},{"kind","summary"},{"text","Quiet afternoon; two deliveries; coffee restocked."},{"timeLabel","15:40"}},
            QVariantMap{{"category","event"},{"kind","motion"},{"text","Motion in Kitchen"},{"timeLabel","15:31"}},
            QVariantMap{{"category","transcript"},{"kind","ambient"},{"text","\"...let's plan the trip this weekend...\""},{"timeLabel","14:58"}},
        }, &stub);

    StubSettings stubSettings;
    auto notifRows = empty ? QVariantList{} : QVariantList{
        QVariantMap{{"id","n1"},{"severity","info"},{"source","system"},{"title","Welcome"},
                    {"body","Polymath is online."},{"timestamp",0},{"timeLabel","16:00"},
                    {"read",false},{"category","notice"}},
        QVariantMap{{"id","n2"},{"severity","good"},{"source","tasks"},{"title","Lab report"},
                    {"body","done — weekly synthesis ready"},{"timestamp",0},{"timeLabel","15:40"},
                    {"read",false},{"category","task"}},
        QVariantMap{{"id","n3"},{"severity","warn"},{"source","reminders"},{"title","Reminder"},
                    {"body","Take out the recycling"},{"timestamp",0},{"timeLabel","14:00"},
                    {"read",true},{"category","reminder"}},
    };
    auto* notifications = new StubNotifications(notifRows, &stub);
    notifications->setUnread(empty ? 0 : 2);

    // --- helper: render one QML file to PNG ---------------------------------
    auto renderView = [&](const QString& qmlUrl, const QString& outPng, bool isWindow) -> bool {
        QQmlApplicationEngine engine;
        QQmlContext* ctx = engine.rootContext();
        ctx->setContextProperty("app", &stub);
        ctx->setContextProperty("chatModel", chat);
        ctx->setContextProperty("shoppingModel", shopping);
        ctx->setContextProperty("cameraModel", cameras);
        ctx->setContextProperty("taskModel", tasks);
        ctx->setContextProperty("timelineModel", timeline);
        // MobileAccessView reads a `gateway` context property at runtime; in the
        // headless harness we feed a stub seeded with a sample pairing payload so
        // the QR encoder + payload box render populated (and scannable).
        ctx->setContextProperty("gateway", &stubGw);
        ctx->setContextProperty("settings", &stubSettings);
        ctx->setContextProperty("notifications", notifications);
        // Software capture path: force faux-glass (no MultiEffect blur).
        ctx->setContextProperty("pmEffectsEnabled", false);

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
        const QString wrapper =
            "import QtQuick\n"
            "import Polymath\n"
            "Window {\n"
            "  width: 1040; height: 760; visible: true; color: Style.bg\n"
            "  Component.onCompleted: Style.effectsEnabled = pmEffectsEnabled\n"
            "  Loader { anchors.fill: parent; source: \"" + qmlUrl + "\";\n"
            "    onStatusChanged: if (status === Loader.Error) console.error('loader error') }\n"
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

    struct View { const char* file; const char* png; bool window; };
    const std::vector<View> views = {
        {"Main.qml",              "01-main-shell",   true },
        {"Dashboard.qml",         "02-dashboard",    false},
        {"ChatView.qml",          "03-chat",         false},
        {"CamerasView.qml",       "04-cameras",      false},
        {"TaskQueueView.qml",     "05-tasks",        false},
        {"TimelineView.qml",      "06-timeline",     false},
        {"ShoppingView.qml",      "07-shopping",     false},
        {"PersonalitiesView.qml", "08-personalities",false},
        {"ModelManagerView.qml",  "09-models",       false},
        {"PrivacyView.qml",       "10-privacy",      false},
        {"MobileAccessView.qml",  "11-mobile-access",false},
        {"SettingsView.qml",      "12-settings",     false},
        {"AgentSessionsView.qml", "13-agents",       false},
    };

    int failures = 0;
    const QString suffix = empty ? "-empty" : "";
    for (const auto& v : views) {
        const QString url = QStringLiteral("qrc:/qt/qml/Polymath/qml/") + v.file;
        const QString out = outDir + "/" + v.png + suffix + ".png";
        const bool ok = renderView(url, out, v.window);
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
