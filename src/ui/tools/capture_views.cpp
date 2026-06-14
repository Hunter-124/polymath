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
    Q_INVOKABLE void setCategoryFilter(const QString& c) { category_filter_ = c; }

    // Count properties the dashboard / headers bind to.  Computed from the row
    // maps so the seeded data and the chips always agree.
    int remainingCount() const { return countWhere([](const QVariantMap& m) {
        return m.contains("done") && !m.value("done").toBool(); }); }
    int doneCount() const { return countWhere([](const QVariantMap& m) {
        return m.contains("done") ? m.value("done").toBool()
                                  : m.value("status").toString() == "done"; }); }
    int queuedCount() const { return countWhere([](const QVariantMap& m) {
        return m.value("status").toString() == "queued"; }); }
    int runningCount() const { return countWhere([](const QVariantMap& m) {
        return m.value("status").toString() == "running"; }); }

private:
    template <typename Pred>
    int countWhere(Pred p) const {
        int n = 0;
        for (const auto& row : rows_) if (p(row.toMap())) ++n;
        return n;
    }

    QHash<int, QByteArray> roles_;
    QVariantList rows_;
    Q_OBJECT
    Q_PROPERTY(QString filter MEMBER filter_)
    Q_PROPERTY(QString categoryFilter MEMBER category_filter_)
    Q_PROPERTY(int remainingCount READ remainingCount CONSTANT)
    Q_PROPERTY(int doneCount      READ doneCount      CONSTANT)
    Q_PROPERTY(int queuedCount    READ queuedCount    CONSTANT)
    Q_PROPERTY(int runningCount   READ runningCount   CONSTANT)
    QString filter_;
    QString category_filter_;
};

// ---------------------------------------------------------------------------
// Stub `app` — the AppController facade surface the QML binds to.
// ---------------------------------------------------------------------------
class StubApp : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
    Q_PROPERTY(QString activePersonality READ activePersonality NOTIFY changed)
    Q_PROPERTY(QVariantMap activePersona READ activePersona NOTIFY changed)
    Q_PROPERTY(bool speaking READ speaking NOTIFY changed)
    Q_PROPERTY(bool controlling READ controlling NOTIFY changed)
    Q_PROPERTY(QString controlAction READ controlAction NOTIFY changed)
    Q_PROPERTY(bool quickAskVisible READ quickAskVisible NOTIFY changed)
    Q_PROPERTY(QString modelStatus READ modelStatus NOTIFY changed)
    Q_PROPERTY(bool hasModels READ hasModels NOTIFY changed)
    Q_PROPERTY(bool firstRun READ firstRun NOTIFY changed)
    Q_PROPERTY(QObject* chatModel READ chatModel CONSTANT)
public:
    bool populated = true;

    bool listening() const { return populated; }
    QString activePersonality() const { return "Marcus Aurelius"; }
    // Talking while populated so the captured Chat/Dashboard show the live state.
    bool speaking() const { return populated; }
    bool controlling() const { return false; }            // overlay hidden in captures
    QString controlAction() const { return QString(); }
    bool quickAskVisible() const { return false; }         // pop-over hidden in captures
    QVariantMap activePersona() const {
        QVariantMap m;
        m["name"]    = "Marcus Aurelius";
        m["style"]   = "orb";
        m["accent"]  = "#7aa2f7";
        m["idle"]    = "";
        m["talking"] = "";
        return m;
    }
    QString modelStatus() const {
        return populated ? "fast: gemma-3n-E4B-it-Q4_K_M" : "no model loaded";
    }
    bool hasModels() const { return populated; }
    bool firstRun() const { return !populated; }
    QObject* chatModel() const { return chat_; }
    void setChat(QObject* m) { chat_ = m; }

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
        out << mk("gemma-3n-E4B-it-Q4_K_M", "fast", 8192, 999, true);
        out << mk("gemma-3-27b-it-Q4_K_M", "heavy", 8192, 46, false);
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
    Q_INVOKABLE void stopControl() {}
    Q_INVOKABLE void showQuickAsk() {}
    Q_INVOKABLE void hideQuickAsk() {}
    Q_INVOKABLE void toggleQuickAsk() {}
    Q_INVOKABLE QString quickAsk(const QString&) { return QString(); }
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
signals:
    void changed();
    void modelsChanged();
    void firstRunChanged();
    void assistantToken(QString, QString, bool);
    void noticePosted(QString, QString, QString);
    void findObjectAnswered(QString, QString);
private:
    QObject* chat_ = nullptr;
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
            "\"pair_code\":\"482915\",\"lan_host\":\"hearth.local\",\"lan_port\":8765}");
    }
    Q_INVOKABLE QString pairingDeepLink() const {
        return QStringLiteral("polymath://pair?home_id=5b80f020-571e-4293-a5ae-0e19c9b814c9"
                              "&code=482915&host=hearth.local&port=8765");
    }
    Q_INVOKABLE bool remoteEnabled() const { return false; }
    Q_INVOKABLE void setRemoteEnabled(bool) {}
    Q_INVOKABLE int  connectedClients() const { return 1; }
signals:
    void connectedClientsChanged(int);
    void remoteEnabledChanged(bool);
};

// ---------------------------------------------------------------------------
// Stub `labModel` — the LabModel surface the Lab cockpit binds to: a sessions
// list plus the live-step banner properties and the steps() invokable.
// ---------------------------------------------------------------------------
class StubLabModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int activeCount READ activeCount NOTIFY changed)
    Q_PROPERTY(qlonglong liveSessionId MEMBER live_session_ NOTIFY changed)
    Q_PROPERTY(QString livePrompt MEMBER live_prompt_ NOTIFY changed)
    Q_PROPERTY(QString liveStatus MEMBER live_status_ NOTIFY changed)
public:
    explicit StubLabModel(bool populated, QObject* parent = nullptr)
        : QAbstractListModel(parent) {
        roles_ = {"sessionId","title","objective","status","startedAt","reportDocId",
                  "stepCount","verifiedCount"};
        if (!populated) return;
        rows_ = QVariantList{
            QVariantMap{{"sessionId",1},{"title","Potassium oxidation"},
                {"objective","Investigate the exothermic oxidation of potassium."},
                {"status","active"},{"reportDocId",0},{"stepCount",5},{"verifiedCount",3}},
            QVariantMap{{"sessionId",2},{"title","Acid–base titration"},
                {"objective","Determine the concentration of the unknown HCl sample."},
                {"status","done"},{"reportDocId",7},{"stepCount",4},{"verifiedCount",4}},
        };
        steps_ = QVariantList{
            QVariantMap{{"stepNo",1},{"prompt","Record the initial mass of the sample."},
                {"measuredValue",4.21},{"measuredUnit","g"},{"verified",true}},
            QVariantMap{{"stepNo",2},{"prompt","Note the starting temperature."},
                {"measuredValue",21.6},{"measuredUnit","°C"},{"verified",true}},
            QVariantMap{{"stepNo",3},{"prompt","Record the peak temperature of the reaction."},
                {"measuredValue",QVariant()},{"measuredUnit",""},{"verified",false}},
        };
        live_session_ = 1;
        live_prompt_  = "Record the peak temperature of the reaction.";
        live_status_  = "ask";
    }
    int rowCount(const QModelIndex& = {}) const override { return rows_.size(); }
    QVariant data(const QModelIndex& idx, int role) const override {
        if (idx.row() < 0 || idx.row() >= rows_.size()) return {};
        const int i = role - (Qt::UserRole + 1);
        if (i < 0 || i >= roles_.size()) return {};
        return rows_.at(idx.row()).toMap().value(roles_.at(i));
    }
    QHash<int, QByteArray> roleNames() const override {
        QHash<int, QByteArray> h; int r = Qt::UserRole + 1;
        for (const auto& n : roles_) h.insert(r++, n.toUtf8());
        return h;
    }
    int activeCount() const {
        int n = 0; for (const auto& row : rows_)
            if (row.toMap().value("status").toString() == "active") ++n;
        return n;
    }
    Q_INVOKABLE QVariantList steps(qlonglong) const { return steps_; }
signals:
    void changed();
private:
    QStringList  roles_;
    QVariantList rows_;
    QVariantList steps_;
    qlonglong    live_session_ = 0;
    QString      live_prompt_;
    QString      live_status_;
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

    auto chat = empty ? new StubListModel({"who","text","streaming","requestId","timeLabel"}, {}, &stub)
        : new StubListModel({"who","text","streaming","requestId","timeLabel"}, QVariantList{
            QVariantMap{{"who","you"},{"text","Where did I leave my keys?"},{"streaming",false},{"timeLabel","16:09"}},
            QVariantMap{{"who","assistant"},{"text","You set them on the **kitchen counter** at 4:12 PM — the camera caught it."},{"streaming",false},{"timeLabel","16:09"}},
            QVariantMap{{"who","you"},{"text","Add milk to the shopping list."},{"streaming",false},{"timeLabel","16:10"}},
            QVariantMap{{"who","assistant"},{"text","Done. Milk is on your list."},{"streaming",true},{"timeLabel","16:10"}},
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

    auto instruments = empty ? new StubListModel({"instrumentId","name","unit","deviceClass","value","inRange","hasReading","ts"}, {}, &stub)
        : new StubListModel({"instrumentId","name","unit","deviceClass","value","inRange","hasReading","ts"}, QVariantList{
            QVariantMap{{"instrumentId","hmm_a1_balance_mass_g"},{"name","Balance"},{"unit","g"},{"deviceClass","mass"},{"value",4.21},{"inRange",true},{"hasReading",true}},
            QVariantMap{{"instrumentId","hmm_a1_hotplate_temp_c"},{"name","Hotplate"},{"unit","°C"},{"deviceClass","temperature"},{"value",72.4},{"inRange",true},{"hasReading",true}},
            QVariantMap{{"instrumentId","hmm_a1_ph"},{"name","pH probe"},{"unit","pH"},{"deviceClass","ph"},{"value",3.1},{"inRange",false},{"hasReading",true}},
            QVariantMap{{"instrumentId","hmm_a1_co2"},{"name","CO₂"},{"unit","ppm"},{"deviceClass","co2"},{"value",612},{"inRange",true},{"hasReading",true}},
            QVariantMap{{"instrumentId","hmm_a1_spec"},{"name","Spectrometer"},{"unit",""},{"deviceClass","spectral"},{"value",0},{"inRange",true},{"hasReading",false}},
        }, &stub);
    StubLabModel lab(!empty, &stub);

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
        ctx->setContextProperty("instrumentModel", instruments);
        ctx->setContextProperty("labModel", &lab);
        // MobileAccessView reads a `gateway` context property at runtime; in the
        // headless harness we feed a stub seeded with a sample pairing payload so
        // the QR encoder + payload box render populated (and scannable).
        ctx->setContextProperty("gateway", &stubGw);

        if (isWindow) {
            // Main.qml is an ApplicationWindow — load it and grab the window.
            engine.load(QUrl(qmlUrl));
            if (engine.rootObjects().isEmpty()) {
                fprintf(stderr, "  load failed %s\n", qPrintable(qmlUrl)); return false;
            }
            auto* win = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            if (!win) { fprintf(stderr, "  not a window %s\n", qPrintable(qmlUrl)); return false; }
            // A fullscreen ApplicationWindow (e.g. PanelMode) has no size offscreen;
            // force it windowed so resize/grab produce a real framebuffer.
            win->setVisibility(QWindow::Windowed);
            win->resize(1280, 820);
            win->show();
            const bool ok = grab(win, outPng);
            win->close();
            return ok;
        }

        // A plain Item view — wrap it in a sized window so it has a surface.
        const QString wrapper =
            "import QtQuick\n"
            "import Polymath\n"
            "Window {\n"
            "  width: 1040; height: 760; visible: true; color: Style.bg\n"
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
        {"Main.qml",              "01-main-shell",    true },
        {"PanelMode.qml",         "02-panel-mode",    true },   // --panel kiosk root (ApplicationWindow)
        {"Dashboard.qml",         "03-dashboard",     false},
        {"ChatView.qml",          "04-chat",          false},
        {"CamerasView.qml",       "05-cameras",       false},
        {"TaskQueueView.qml",     "06-tasks",         false},
        {"TimelineView.qml",      "07-timeline",      false},
        {"ShoppingView.qml",      "08-shopping",      false},
        {"LabView.qml",           "09-lab",           false},
        {"PersonalitiesView.qml", "10-personalities", false},
        {"ModelManagerView.qml",  "11-models",        false},
        {"PrivacyView.qml",       "12-privacy",       false},
        {"MobileAccessView.qml",  "13-mobile-access", false},
        {"SettingsView.qml",      "14-settings",      false},
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
    // Avatar gallery — a dedicated showcase of the procedural PersonalityAvatar
    // across styles and idle/speaking states (rendered once, on the populated run).
    if (!empty) {
        const QString galleryQml =
            "import QtQuick\n"
            "import Polymath\n"
            "Window {\n"
            "  width: 980; height: 360; visible: true; color: Style.bg\n"
            "  Column {\n"
            "    anchors.centerIn: parent; spacing: 24\n"
            "    Row {\n"
            "      spacing: 50\n"
            "      Repeater {\n"
            "        model: [\n"
            "          { n: 'Marcus Aurelius', s: 'orb',  a: '#7aa2f7', sp: false, cap: 'orb \\u00b7 idle' },\n"
            "          { n: 'Marcus Aurelius', s: 'orb',  a: '#7aa2f7', sp: true,  cap: 'orb \\u00b7 speaking' },\n"
            "          { n: 'Ada Lovelace',    s: 'bars', a: '#bb9af7', sp: true,  cap: 'bars \\u00b7 speaking' },\n"
            "          { n: 'Lab Guide',       s: 'ring', a: '#9ece6a', sp: true,  cap: 'ring \\u00b7 speaking' }\n"
            "        ]\n"
            "        delegate: Column {\n"
            "          required property var modelData\n"
            "          spacing: 12\n"
            "          PersonalityAvatar {\n"
            "            anchors.horizontalCenter: parent.horizontalCenter\n"
            "            width: 96; height: 96\n"
            "            displayName: modelData.n; avatarStyle: modelData.s\n"
            "            accent: modelData.a; speaking: modelData.sp\n"
            "          }\n"
            "          Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.cap\n"
            "            color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: 13 }\n"
            "        }\n"
            "      }\n"
            "    }\n"
            "    Text { anchors.horizontalCenter: parent.horizontalCenter\n"
            "      text: 'PersonalityAvatar \\u2014 procedural, theme-tinted, alive while speaking'\n"
            "      color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: 12 }\n"
            "  }\n"
            "}\n";
        QQmlApplicationEngine gengine;
        gengine.rootContext()->setContextProperty("app", &stub);
        QQmlComponent comp(&gengine);
        comp.setData(galleryQml.toUtf8(), QUrl("qrc:/avatar_gallery.qml"));
        QObject* obj = comp.create(gengine.rootContext());
        bool ok = false;
        if (!obj) {
            fprintf(stderr, "  avatar gallery failed: %s\n", qPrintable(comp.errorString()));
        } else if (auto* win = qobject_cast<QQuickWindow*>(obj)) {
            win->show();
            ok = grab(win, outDir + "/15-avatars.png");
            win->close();
        }
        delete obj;
        fprintf(stderr, "%s  %s\n", ok ? "OK  " : "FAIL", qPrintable(outDir + "/15-avatars.png"));
        if (!ok) ++failures;
    }

    fprintf(stderr, "captured %d / %d views%s\n",
            int(views.size()) - failures, int(views.size()),
            empty ? " (empty/first-run states)" : "");
    fflush(stderr);
    return failures == 0 ? 0 : 3;
}

#include "capture_views.moc"
