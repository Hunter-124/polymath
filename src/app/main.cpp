#include "app_controller.h"
#include "paths.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QCoreApplication>

using namespace polymath;

static std::filesystem::path resolveAppRoot() {
    // Portable layout: a `data/` folder beside the executable.  (An installer
    // build would point this at %LOCALAPPDATA%/Polymath instead.)
    QDir base(QCoreApplication::applicationDirPath());
    return std::filesystem::path(base.absoluteFilePath("data").toStdWString());
}

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Polymath");
    QCoreApplication::setApplicationName("Polymath");
    QQuickStyle::setStyle("Basic");   // self-contained style -> static-friendly

    Paths::instance().setRoot(resolveAppRoot());

    AppController controller;
    if (!controller.initialize())
        return 1;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("app", &controller);

    // Register the Wave-3 data models (context properties) and the camera image
    // provider ("image://cameras/<id>") before the QML is loaded.
    controller.registerWithEngine(engine);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);

    engine.loadFromModule("Polymath", "Main");
    if (engine.rootObjects().isEmpty())
        return 2;

    const int rc = app.exec();
    controller.shutdown();
    return rc;
}
