#include "app_controller.h"
#include "paths.h"
#include "logging.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QtGlobal>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#endif

using namespace polymath;

// A startup failure in a WIN32 (GUI-subsystem) build is otherwise completely
// silent — the process exits and the user sees nothing at all.  Log it AND put
// a native message box on screen (no Qt widgets dependency) so "the app does
// nothing when I launch it" is never the failure mode again.
static void reportFatalStartupError(const QString& what) {
    PM_ERROR("startup failed: {}", what.toStdString());
#ifdef Q_OS_WIN
    const QString msg = what +
        "\n\nDetails may be in  data\\logs\\polymath.log  next to the executable.";
    MessageBoxW(nullptr,
                reinterpret_cast<const wchar_t*>(msg.utf16()),
                L"Hearth could not start",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
#endif
}

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

    // --panel  →  load PanelMode.qml fullscreen (touch/kiosk mode) instead of
    //             the normal Main.qml desktop shell.
    QCommandLineParser cli;
    cli.setApplicationDescription("Hearth — Local AI Home Assistant");
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption panelOpt(QStringList{"panel"},
        "Start in fullscreen touch/kiosk panel mode (loads PanelMode.qml).");
    cli.addOption(panelOpt);
    cli.process(app);
    const bool panelMode = cli.isSet(panelOpt);

    Paths::instance().setRoot(resolveAppRoot());

    AppController controller;
    if (!controller.initialize()) {
        reportFatalStartupError(
            "Core services failed to initialize (database or service startup).");
        return 1;
    }

    qInstallMessageHandler(qtMessageToLog);   // Qt/QML diagnostics -> our log

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("app", &controller);

    // Register the Wave-3 data models (context properties) and the camera image
    // provider ("image://cameras/<id>") before the QML is loaded.
    controller.registerWithEngine(engine);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] {
                         reportFatalStartupError(
                             "The user interface failed to load (QML object creation failed). "
                             "The install may be missing Qt QML modules under qml\\.");
                         QCoreApplication::exit(2);
                     }, Qt::QueuedConnection);

    // Load the embedded scene by resource URL. (Loading by module name would
    // require importing the static QML module's plugin into the exe; the URL is
    // deterministic and the QML files only import standard Qt modules.)
    //
    // --panel swaps in PanelMode.qml (fullscreen touch/kiosk dashboard) while
    // keeping every other initialisation path identical.
    const QString rootQml = panelMode
        ? QStringLiteral("qrc:/qt/qml/Polymath/qml/PanelMode.qml")
        : QStringLiteral("qrc:/qt/qml/Polymath/qml/Main.qml");
    engine.load(QUrl(rootQml));
    if (engine.rootObjects().isEmpty()) {
        reportFatalStartupError(
            "The user interface failed to load (no root window was created). "
            "The install may be missing Qt QML modules under qml\\.");
        return 2;
    }

    const int rc = app.exec();
    controller.shutdown();
    return rc;
}
