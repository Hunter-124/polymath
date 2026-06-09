#pragma once
//
// IAssistantBridge — the narrow seam between the gateway and the rest of the app.
//
// WHY THIS EXISTS
//   pm_gateway must depend ONLY on pm_core + Qt, never on pm_app, or we create a
//   dependency cycle (pm_app owns AppController, which will in turn own the
//   GatewayService).  So instead of calling AppController directly, the gateway
//   talks to this abstract interface.  During the deferred wiring step,
//   AppController will inherit IAssistantBridge (or hold a thin adapter that
//   does) and forward each call to its existing Q_INVOKABLE actions — the method
//   names here deliberately mirror AppController's surface:
//
//       AppController::sendChat / sendText   -> sendChat
//       AppController::setPersonality        -> setPersonality
//       AppController::personalities         -> personalities
//       AppController::setPrivacy            -> setPrivacy
//       AppController::privacy               -> privacy
//       AppController::findObject            -> findObject
//       AppController::addShoppingItem       -> addShoppingItem
//       AppController::models                -> models
//       AppController::listening             -> listening
//       AppController::activePersonality     -> activePersonality
//       AppController::modelStatus           -> modelStatus
//
// THREADING
//   GatewayService runs on its own QThread, so the HTTP/WS handlers invoke these
//   methods from a worker thread.  The implementer (AppController) must make each
//   call thread-safe.  In practice AppController's actions already marshal onto
//   the right worker via QMetaObject::invokeMethod(..., Qt::QueuedConnection),
//   and its read-only getters touch only atomics / the WAL-mode Database, so the
//   forwarding adapter is a straight pass-through.  Anything that must return a
//   value synchronously (personalities(), privacy(), models(), the status
//   getters) should be cheap and lock-free or DB-backed.
//
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace polymath {

// Pure-virtual bridge.  No QObject: it carries no signals (events reach the
// gateway via the EventBus, not through here) and stays trivially implementable.
class IAssistantBridge {
public:
    virtual ~IAssistantBridge() = default;

    // --- actions ---------------------------------------------------------

    // Submit a chat turn.  Returns the request_id that correlates the streamed
    // `token` WS events (mirrors AppController::sendChat + the rid it mints).
    // The implementation appends the user turn to the ChatModel and dispatches
    // to the agent worker.
    virtual QString sendChat(const QString& text) = 0;

    // Switch the active personality by name.
    virtual void setPersonality(const QString& name) = 0;

    // List available personality names.
    virtual QStringList personalities() = 0;

    // Toggle a privacy.* setting (also emits PrivacyChanged on the EventBus, so
    // connected clients see a `privacy` event).
    virtual void setPrivacy(const QString& key, bool enabled) = 0;

    // Read a privacy.* toggle (defaults ON, per the product decision).
    virtual bool privacy(const QString& key) = 0;

    // Kick off a vision "find object" query; the answer arrives later as a
    // findObjectDone EventBus signal -> `find_object` WS event.
    virtual void findObject(const QString& query) = 0;

    // Append an item to the shopping list (keeps the UI model + DB in sync).
    virtual void addShoppingItem(const QString& item) = 0;

    // Registered inference models for the Model Manager, as a list of QVariantMap
    // {id, displayName, role, path, nCtx, nGpuLayers, active} — the exact shape
    // AppController::models() returns.  json_map normalizes it to ModelDTO.
    virtual QVariantList models() = 0;

    // --- status getters --------------------------------------------------

    virtual bool    listening() = 0;          // mic/ASR active
    virtual QString activePersonality() = 0;  // e.g. "Assistant"
    virtual QString modelStatus() = 0;        // e.g. "fast: qwen2.5-7b" or "no model loaded"
};

} // namespace polymath
