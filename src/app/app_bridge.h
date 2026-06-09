#pragma once
//
// AppBridge — adapts AppController to the gateway's IAssistantBridge seam.
//
// pm_gateway must not depend on pm_app (that would be a dependency cycle), so it
// talks to the rest of the app only through IAssistantBridge (src/gateway/
// bridge.h). AppBridge is the thin implementation that forwards each call to the
// owning AppController, which it holds by reference (no ownership).
//
// THREADING: the gateway invokes these methods from its own worker thread.
//   * Actions that mutate UI-thread objects (the personality manager, the
//     shopping list model, the chat model) are marshaled onto the
//     AppController's thread via a queued QMetaObject::invokeMethod, so the only
//     thread that touches those QObjects is the one that owns them.
//   * Read-only getters touch only the WAL-mode Database or plain status members
//     and are forwarded directly, matching the contract documented in bridge.h.
//
#include "bridge.h"

namespace polymath {

class AppController;

class AppBridge : public IAssistantBridge {
public:
    explicit AppBridge(AppController& app) : app_(app) {}

    // --- actions ---------------------------------------------------------
    QString      sendChat(const QString& text) override;
    void         setPersonality(const QString& name) override;
    QStringList  personalities() override;
    void         setPrivacy(const QString& key, bool enabled) override;
    bool         privacy(const QString& key) override;
    void         findObject(const QString& query) override;
    void         addShoppingItem(const QString& item) override;
    QVariantList models() override;

    // --- status getters --------------------------------------------------
    bool    listening() override;
    QString activePersonality() override;
    QString modelStatus() override;

private:
    AppController& app_;
};

} // namespace polymath
