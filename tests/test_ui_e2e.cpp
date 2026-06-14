// ---------------------------------------------------------------------------
//  UI render end-to-end  (Wave 2 · Card F)
// ---------------------------------------------------------------------------
//
//  Loads the Main shell and all 9 views offscreen against a *stub* `app` +
//  stub list models that mirror the AppController surface the QML binds to, and
//  asserts every view instantiates with ZERO QML errors.  This guards the
//  contract between the QML (Card F) and the C++ facade: a renamed role, a typo
//  in an `import Polymath` type, a binding to a property the views expect — all
//  surface here as a load error rather than a blank panel at runtime.
//
//  No backend, models, GPU or display: QT_QPA_PLATFORM=offscreen (set by the
//  test's ENVIRONMENT) and a stub context.  It links pm_ui to pull in the
//  embedded `Polymath` QML module (Style singleton + themed controls), which
//  resolves from resources because the module is built NO_PLUGIN.
//
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QAbstractListModel>
#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QUrl>
#include <cstdio>
#include <vector>

namespace {

// A role-named list model mirroring the app's QAbstractListModels.
class StubListModel : public QAbstractListModel {
public:
    StubListModel(QStringList roles, QVariantList rows, QObject* parent)
        : QAbstractListModel(parent), rows_(std::move(rows)) {
        int r = Qt::UserRole + 1;
        for (const auto& n : roles) roles_.insert(r++, n.toUtf8());
    }
    int rowCount(const QModelIndex& = {}) const override { return rows_.size(); }
    QVariant data(const QModelIndex& i, int role) const override {
        if (i.row() < 0 || i.row() >= rows_.size()) return {};
        return rows_.at(i.row()).toMap().value(QString::fromUtf8(roles_.value(role)));
    }
    QHash<int, QByteArray> roleNames() const override { return roles_; }
    Q_INVOKABLE void refresh() {}
    Q_INVOKABLE void addItem(const QString&, const QString& = {}) {}
    Q_INVOKABLE void setDone(int, bool) {}
    Q_INVOKABLE void removeItem(int) {}
    Q_INVOKABLE void clearDone() {}
    Q_INVOKABLE void setFilter(const QString&) {}
private:
    QHash<int, QByteArray> roles_;
    QVariantList rows_;
    Q_OBJECT
    Q_PROPERTY(QString filter MEMBER filter_)
    QString filter_;
};

// The AppController facade surface the QML binds to.
class StubApp : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
    Q_PROPERTY(QString activePersonality READ activePersonality NOTIFY changed)
    Q_PROPERTY(bool quickAskVisible READ quickAskVisible NOTIFY changed)
    Q_PROPERTY(QString modelStatus READ modelStatus NOTIFY changed)
    Q_PROPERTY(bool hasModels READ hasModels NOTIFY changed)
    Q_PROPERTY(bool firstRun  READ firstRun  NOTIFY changed)
    Q_PROPERTY(QObject* chatModel READ chatModel CONSTANT)
public:
    bool listening() const { return true; }
    QString activePersonality() const { return "Assistant"; }
    bool quickAskVisible() const { return false; }
    QString modelStatus() const { return "fast: gemma-3n"; }
    bool hasModels() const { return true; }
    bool firstRun() const { return false; }
    QObject* chatModel() const { return chat_; }
    void setChat(QObject* c) { chat_ = c; }
    Q_INVOKABLE QStringList personalities() const { return {"Assistant", "Ada Lovelace"}; }
    Q_INVOKABLE QVariantList models() const {
        QVariantMap m; m["id"] = "m"; m["displayName"] = "m"; m["role"] = "fast"; m["nCtx"] = 8192;
        m["nGpuLayers"] = 999; m["active"] = true; m["path"] = "data/models/m.gguf";
        return QVariantList{ m };
    }
    Q_INVOKABLE bool privacy(const QString&) const { return true; }
    Q_INVOKABLE void sendChat(const QString&) {}
    Q_INVOKABLE void sendText(const QString&) {}
    Q_INVOKABLE void pushToTalk(bool) {}
    Q_INVOKABLE void setPersonality(const QString&) {}
    Q_INVOKABLE void setPrivacy(const QString&, bool) {}
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
    Q_INVOKABLE bool addModel(const QString&, const QString&) { return true; }
    Q_INVOKABLE void setModelRole(const QString&, const QString&) {}
    Q_INVOKABLE void completeFirstRun() {}
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

} // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");

    StubApp stub;
    auto chat = new StubListModel({"who","text","streaming","requestId"},
        QVariantList{ QVariantMap{{"who","you"},{"text","hi"},{"streaming",false}} }, &stub);
    stub.setChat(chat);
    auto shopping = new StubListModel({"itemId","item","quantity","done","createdAt"},
        QVariantList{ QVariantMap{{"item","Milk"},{"quantity","2L"},{"done",false}} }, &stub);
    auto cameras = new StubListModel({"cameraId","name","location","enabled","live","frameTick"},
        QVariantList{ QVariantMap{{"cameraId",1},{"name","Front"},{"location","x"},{"enabled",true},{"live",true},{"frameTick",1}} }, &stub);
    auto tasks = new StubListModel({"taskId","type","status","detail","priority"},
        QVariantList{ QVariantMap{{"type","Sum"},{"status","running"},{"detail","d"},{"priority",1}} }, &stub);
    auto timeline = new StubListModel({"category","kind","text","timestamp","timeLabel"},
        QVariantList{ QVariantMap{{"category","event"},{"kind","person"},{"text","t"},{"timeLabel","12:00"}} }, &stub);

    const std::vector<const char*> views = {
        "Main.qml", "Dashboard.qml", "ChatView.qml", "CamerasView.qml",
        "TaskQueueView.qml", "TimelineView.qml", "ShoppingView.qml",
        "PersonalitiesView.qml", "ModelManagerView.qml", "PrivacyView.qml",
    };

    int failures = 0;
    for (const char* v : views) {
        QQmlEngine engine;
        QQmlContext* ctx = engine.rootContext();
        ctx->setContextProperty("app", &stub);
        ctx->setContextProperty("chatModel", chat);
        ctx->setContextProperty("shoppingModel", shopping);
        ctx->setContextProperty("cameraModel", cameras);
        ctx->setContextProperty("taskModel", tasks);
        ctx->setContextProperty("timelineModel", timeline);

        const QString url = QStringLiteral("qrc:/qt/qml/Polymath/qml/") + v;
        QQmlComponent comp(&engine, QUrl(url));
        QObject* obj = comp.create(ctx);
        if (comp.isError() || !obj) {
            ++failures;
            fprintf(stderr, "FAIL  %s\n", v);
            for (const auto& e : comp.errors())
                fprintf(stderr, "      %s\n", qPrintable(e.toString()));
        } else {
            fprintf(stderr, "OK    %s\n", v);
        }
        delete obj;
    }

    fprintf(stderr, "\nUI render test: %d/%d views loaded clean\n",
            int(views.size()) - failures, int(views.size()));
    return failures == 0 ? 0 : 1;
}

#include "test_ui_e2e.moc"
