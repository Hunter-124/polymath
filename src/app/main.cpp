#include "app_controller.h"
#include "paths.h"
#include "logging.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QCoreApplication>
#include <QtGlobal>

using namespace polymath;

// Route Qt/QML diagnostics into our log. Without this they vanish in a WIN32
// (GUI-subsystem) build, which has no console.
static void qtMessageToLog(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    const std::string m = msg.toStdString();
    const char* where = ctx.file ? ctx.file : "";
    switch (type) {
        case QtDebugMsg:    PM_DEBUG("[qt] {}", m); break;
        case QtInfoMsg:     PM_INFO("[qt] {}", m); break;
        case QtWarningMsg:  PM_WARN("[qt] {} ({})", m, where); break;
        case QtCriticalMsg: PM_ERROR("[qt] {} ({})", m, where); break;
        case QtFatalMsg:    PM_ERROR("[qt-fatal] {} ({})", m, where); break;
    }
}

static std::filesystem::path resolveAppRoot() {
    // Portable layout: a `data/` folder beside the executable.  (An installer
    // build would point this at %LOCALAPPDATA%/Hearth instead.)
    QDir base(QCoreApplication::applicationDirPath());
    return std::filesystem::path(base.absoluteFilePath("data").toStdWString());
}

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Hearth");
    QCoreApplication::setApplicationName("Hearth");
    QQuickStyle::setStyle("Basic");   // self-contained style -> static-friendly

    Paths::instance().setRoot(resolveAppRoot());

    AppController controller;
    if (!controller.initialize())
        return 1;

    qInstallMessageHandler(qtMessageToLog);   // Qt/QML diagnostics -> our log

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("app", &controller);

    // Register the Wave-3 data models (context properties) and the camera image
    // provider ("image://cameras/<id>") before the QML is loaded.
    controller.registerWithEngine(engine);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);

    // Load the embedded scene by resource URL. (Loading by module name would
    // require importing the static QML module's plugin into the exe; the URL is
    // deterministic and the QML files only import standard Qt modules.)
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Polymath/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;

    const int rc = app.exec();
    controller.shutdown();
    return rc;
}
