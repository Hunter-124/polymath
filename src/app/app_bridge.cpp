#include "app_bridge.h"
#include "app_controller.h"

#include <QMetaObject>

namespace polymath {

// Actions that mutate UI-thread-owned objects are posted to the AppController's
// thread. invokeMethod with the default AutoConnection is direct when the caller
// is already on that thread and queued otherwise — so a QML-side caller stays
// synchronous while the gateway worker thread hops on safely.

QString AppBridge::sendChat(const QString& text) {
    // submitChatTurn is itself thread-safe (it marshals the chat-model append)
    // and returns the request_id the client correlates streamed tokens to.
    return app_.submitChatTurn(text);
}

void AppBridge::setPersonality(const QString& name) {
    QMetaObject::invokeMethod(&app_, [&a = app_, name] { a.setPersonality(name); });
}

QStringList AppBridge::personalities() { return app_.personalities(); }

void AppBridge::setPrivacy(const QString& key, bool enabled) {
    QMetaObject::invokeMethod(&app_, [&a = app_, key, enabled] { a.setPrivacy(key, enabled); });
}

bool AppBridge::privacy(const QString& key) { return app_.privacy(key); }

void AppBridge::findObject(const QString& query) {
    QMetaObject::invokeMethod(&app_, [&a = app_, query] { a.findObject(query); });
}

void AppBridge::addShoppingItem(const QString& item) {
    QMetaObject::invokeMethod(&app_, [&a = app_, item] { a.addShoppingItem(item); });
}

QVariantList AppBridge::models() { return app_.models(); }

bool    AppBridge::listening()         { return app_.listening(); }
QString AppBridge::activePersonality() { return app_.activePersonality(); }
QString AppBridge::modelStatus()       { return app_.modelStatus(); }

} // namespace polymath
