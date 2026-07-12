#include "app_controller.h"
#include "paths.h"
#include "logging.h"
#include "single_instance.h"
#include "web_adblock_interceptor.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QFont>
#include <QFontDatabase>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QtGlobal>
#include <QtWebEngineQuick>
#include <QWebEngineProfile>

#include <exception>

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

// True if we can actually create+write a file under `dir` (creating it first).
// A read-only install location (e.g. C:\Program Files\Polymath for a non-admin
// user) fails here — that is precisely the case that must fall back to a
// per-user writable root, or spdlog/sqlite throw at startup and the app aborts.
static bool isWritableDir(const QString& dir) {
    QDir().mkpath(dir);
    const QString probe = QDir(dir).absoluteFilePath(".write-probe");
    QFile f(probe);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.close();
    QFile::remove(probe);
    return true;
}

static std::filesystem::path resolveAppRoot() {
    // Portable layout: a writable `data/` folder beside the executable. This is
    // how the dev builds and the portable zip run.
    const QDir base(QCoreApplication::applicationDirPath());
    const QString portable = base.absoluteFilePath("data");
    if (isWritableDir(portable))
        return std::filesystem::path(portable.toStdWString());

    // Installed layout: the exe lives somewhere read-only (Program Files). Fall
    // back to a per-user writable root — %LOCALAPPDATA%/Polymath/Polymath on
    // Windows (organization + application name, set in main() before this runs).
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(appData);
    return std::filesystem::path(appData.toStdWString());
}

int main(int argc, char* argv[]) try {
    // D5: WebEngine requires shared GL contexts + initialize() before the app.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // Stabilize embedded Chromium on Windows hybrid-GPU laptops (e.g. RTX Max-Q
    // + Intel iGPU). Blank WebEngineViews are often GPU-process / occlusion bugs.
    // Autoplay without a click is required for AI-spawned YouTube surfaces.
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS")) {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
                "--autoplay-policy=no-user-gesture-required "
                "--disable-features=CalculateNativeWinOcclusion "
                "--ignore-gpu-blocklist");
    }
    // Point Chromium at the deployed helper next to Polymath.exe *before*
    // QtWebEngineQuick::initialize() so the subprocess path is fixed.
    if (qEnvironmentVariableIsEmpty("QTWEBENGINEPROCESS_PATH") && argc > 0 && argv[0]) {
        const QFileInfo exeInfo(QString::fromLocal8Bit(argv[0]));
        const QString proc = exeInfo.absolutePath()
                             + QStringLiteral("/QtWebEngineProcess.exe");
        if (QFileInfo::exists(proc))
            qputenv("QTWEBENGINEPROCESS_PATH", QFile::encodeName(proc));
    }

    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Polymath");
    QCoreApplication::setApplicationName("Polymath");
    QQuickStyle::setStyle("Basic");   // self-contained style -> static-friendly
    // Main.qml hides to the system tray on close; without this, the last window
    // disappearing would still quit the process and look like a "crash".
    QGuiApplication::setQuitOnLastWindowClosed(false);

    // Replace-on-launch: kill any prior/stale Polymath.exe still alive, then
    // hold a session mutex so we never run two real instances at once.
    // Override with --allow-multiple for debugging.
    {
        const SingleInstanceResult si = claimSingleInstance();
        if (!si.ok) {
            qCritical("Polymath: single-instance claim failed: %s",
                      si.detail.c_str());
            return 4;
        }
        if (si.killed > 0)
            qInfo("Polymath: terminated %d prior instance(s) — %s",
                  si.killed, si.detail.c_str());
    }

    // Load Inter before any QML paints so Qt never falls through to broken
    // "MS Sans Serif" / "Inter Variable" DirectWrite paths on first frame.
    {
        const int fid = QFontDatabase::addApplicationFont(
            QStringLiteral(":/qt/qml/Polymath/fonts/Inter.ttf"));
        QString family = QStringLiteral("Inter");
        if (fid >= 0) {
            const auto families = QFontDatabase::applicationFontFamilies(fid);
            if (!families.isEmpty()) {
                // Prefer a non-"Variable" family name when the TTF reports one.
                for (const auto& f : families) {
                    if (f.startsWith(QStringLiteral("Inter")) &&
                        !f.contains(QStringLiteral("Variable"), Qt::CaseInsensitive)) {
                        family = f;
                        break;
                    }
                }
                if (family == QStringLiteral("Inter") && !families.isEmpty())
                    family = families.first().contains(QStringLiteral("Variable"))
                                 ? QStringLiteral("Inter")
                                 : families.first();
            }
        }
        QFont appFont(family, 10);
        appFont.setStyleStrategy(QFont::PreferAntialias);
        app.setFont(appFont);
    }

    Paths::instance().setRoot(resolveAppRoot());

    AppController controller;
    if (!controller.initialize())
        return 1;

    qInstallMessageHandler(qtMessageToLog);   // Qt/QML diagnostics -> our log

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [] {
        PM_INFO("Polymath shutting down (aboutToQuit)");
    });

    // D5: adblock interceptor on the default profile (all WebSurface views).
    auto* adblock = new WebAdblockInterceptor(&app);
    auto* profile = QWebEngineProfile::defaultProfile();
    profile->setUrlRequestInterceptor(adblock);
    // YouTube's embed player is picky about UA (Error 153). Use a plain desktop
    // Chrome UA — no custom product token (sites treat "PolymathWeb" as a bot
    // and break Search / interactive chrome). Keep Chrome major near Qt 6.6's
    // Chromium (~112) so Client Hints don't fight the real engine.
    profile->setHttpUserAgent(
        QStringLiteral(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/112.0.0.0 Safari/537.36"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("app", &controller);
    engine.rootContext()->setContextProperty("webAdblock", adblock);

    // YtClean.js for WebSurface — load from qrc in C++. QML XMLHttpRequest to
    // local/qrc is disabled by default in Qt 6.5+ ("Using GET on a local file
    // is disabled"), which left ytCleanScript empty and clean-mode dead.
    {
        QFile ytClean(QStringLiteral(":/qt/qml/Polymath/qml/surfaces/YtClean.js"));
        QString ytCleanText;
        if (ytClean.open(QIODevice::ReadOnly | QIODevice::Text))
            ytCleanText = QString::fromUtf8(ytClean.readAll());
        else
            qWarning("Polymath: could not load YtClean.js from qrc");
        engine.rootContext()->setContextProperty(
            QStringLiteral("ytCleanScriptText"), ytCleanText);
    }

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
// A startup failure (e.g. an unwritable data root, a corrupt DB, a missing
// runtime) used to escape main() and abort the process with a cryptic
// 0xC0000409 in ucrtbase — the app "installed but wouldn't launch". Catch it so
// we exit with a clean, non-zero status the launcher/first-run scripts can act
// on instead of crash-on-open.
catch (const std::exception& e) {
    qCritical("Polymath failed to start: %s", e.what());
    return 3;
}
catch (...) {
    qCritical("Polymath failed to start: unknown error");
    return 3;
}
