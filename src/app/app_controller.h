#pragma once
//
// AppController — the single QObject facade exposed to QML (context property
// "app").  It owns the Database, constructs every backend service, moves each
// onto its own QThread, wires them together through the EventBus, and surfaces
// state/actions to the UI.  The Wave-3 UI agent extends this with the
// QAbstractListModels (chat, shopping, cameras, tasks, timeline).
//
#include "database.h"
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <memory>
#include <vector>

class QThread;
class QQmlApplicationEngine;

namespace polymath {

class InferenceManager;
class TaskScheduler;
class ProactiveEngine;
class IdleDetector;
class AudioService;
class VisionService;
class MemoryService;
class AgentRuntime;
class PersonalityManager;
class Config;
class AppBridge;
class GatewayService;

// Wave-3 UI data layer (QAbstractListModels + the live-frame image provider).
class ChatModel;
class ShoppingModel;
class CameraModel;
class TaskModel;
class ScheduledGoalsModel;
class TimelineModel;
class CameraImageProvider;
class SettingsController;
class NotificationsModel;
class SessionsModel;
class AgentSessionService;
class PersonalityModel;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(QString activePersonality READ activePersonality NOTIFY activePersonalityChanged)
    Q_PROPERTY(QString modelStatus READ modelStatus NOTIFY modelStatusChanged)
    // First-run / cold-start affordances. hasModels = at least one usable model
    // is registered on disk; firstRun = no usable model AND the first-run flow has
    // not been acknowledged. Both notify when model state changes (wired to
    // InferenceManager::modelStateChanged) or when first-run is completed.
    Q_PROPERTY(bool hasModels READ hasModels NOTIFY modelsChanged)
    Q_PROPERTY(bool firstRun  READ firstRun  NOTIFY firstRunChanged)
    // Wave-3 UI: data models exposed to QML (constructed in initialize()).
    Q_PROPERTY(QObject* chatModel     READ chatModel     CONSTANT)
    Q_PROPERTY(QObject* shoppingModel READ shoppingModel CONSTANT)
    Q_PROPERTY(QObject* cameraModel   READ cameraModel   CONSTANT)
    Q_PROPERTY(QObject* taskModel     READ taskModel     CONSTANT)
    Q_PROPERTY(QObject* scheduledGoalsModel READ scheduledGoalsModel CONSTANT)
    Q_PROPERTY(QObject* timelineModel READ timelineModel CONSTANT)
    // E2: personality editor's list model (mirrors PersonalityManager::all()).
    Q_PROPERTY(QObject* personalityModel READ personalityModel CONSTANT)
    // Overhaul A2: VRAM HUD + wake pulse + settings/notifications facades.
    Q_PROPERTY(int vramUsedMiB  READ vramUsedMiB  NOTIFY vramChanged)
    Q_PROPERTY(int vramTotalMiB READ vramTotalMiB NOTIFY vramChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    bool initialize();    // open DB, seed config, build + start services
    void shutdown();

    // Registers the data models as context properties and installs the camera
    // image provider on `engine`.  Call from main.cpp after initialize() and
    // before loading the QML root (it is additive — the legacy "app" context
    // property still works for code that uses app.chatModel etc.).
    void registerWithEngine(QQmlApplicationEngine& engine);

    // The mobile/web gateway service (LAN HTTP+WS + optional relay tunnel), so
    // main.cpp / QML can reach it (exposed to QML as the "gateway" context
    // property for the Settings ▸ Mobile Access pairing UI).  Null until
    // initialize() has run; lives on its own QThread.
    QObject* gateway() const;

    bool    listening() const { return listening_; }
    QString activePersonality() const { return active_personality_; }
    QString modelStatus() const { return model_status_; }
    bool    hasModels() const;
    bool    firstRun() const;

    // Model accessors (return QObject* so they bind cleanly as Q_PROPERTYs).
    QObject* chatModel() const;
    QObject* shoppingModel() const;
    QObject* cameraModel() const;
    QObject* taskModel() const;
    QObject* scheduledGoalsModel() const;
    QObject* timelineModel() const;
    QObject* personalityModel() const;

    int vramUsedMiB() const { return vram_used_mib_; }
    int vramTotalMiB() const { return vram_total_mib_; }

    // --- QML-callable actions ---
    Q_INVOKABLE void sendText(const QString& text);
    Q_INVOKABLE void pushToTalk(bool down);
    Q_INVOKABLE void setPersonality(const QString& name);
    Q_INVOKABLE QStringList personalities() const;

    // --- E2: personality editor pass-throughs (PersonalityManager write API) ---
    // See PersonalityManager::createBundle/saveBundle/setAvatar/deleteBundle
    // for the exact contract; these just forward with a null-guard since
    // personality_ isn't constructed until initialize() has run.
    Q_INVOKABLE bool createPersonality(const QString& name);
    Q_INVOKABLE bool savePersonality(const QString& name, const QString& json);
    Q_INVOKABLE bool setPersonalityAvatar(const QString& name, const QString& sourcePath);
    Q_INVOKABLE bool deletePersonality(const QString& name);
    // Tool names the editor's allow-list multi-select offers (ToolRegistry's
    // full registered set — the persona's own `tools` array is the subset
    // filter, not this list). Empty until AgentRuntime has registered its
    // built-in tools (happens moments after start(), on the agent thread).
    Q_INVOKABLE QStringList availableToolNames() const;
    // Model choices for the "preferred model" combo: the two role keywords
    // the runtime resolves at call time ("fast"/"heavy") plus every specific
    // registered model id, for personas that want to pin an exact model.
    Q_INVOKABLE QStringList availableModels() const;
    Q_INVOKABLE void setPrivacy(const QString& key, bool enabled);
    Q_INVOKABLE bool privacy(const QString& key) const;
    Q_INVOKABLE void findObject(const QString& query);
    Q_INVOKABLE void addShoppingItem(const QString& item);

    // --- Wave-3 UI helpers ---
    // Reload every table-backed model from SQLite (call when a view becomes
    // visible).  Cheap, bounded reads on the UI thread.
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE void refreshShopping();
    Q_INVOKABLE void refreshCameras();
    Q_INVOKABLE void refreshTasks();
    Q_INVOKABLE void refreshSchedules();
    Q_INVOKABLE void refreshTimeline();

    // Send chat text, appending the user turn to the ChatModel and correlating
    // the streamed reply.  Thin wrapper over sendText() the ChatView calls.
    Q_INVOKABLE void sendChat(const QString& text);

    // Submit a chat turn and return the request_id that correlates the streamed
    // `token` events.  Appends the user turn to the ChatModel (marshaled onto the
    // UI thread) and dispatches to the agent worker.  Backs both the QML sendChat
    // path and the mobile gateway's IAssistantBridge::sendChat, which needs the
    // rid to hand back to the phone/PWA client.  Thread-safe to call from any
    // thread.
    QString submitChatTurn(const QString& text);

    // Registered inference models (the `models` table) for the Model Manager UI,
    // as a list of maps {id,displayName,role,path,nCtx,nGpuLayers,active}.
    Q_INVOKABLE QVariantList models() const;

    // --- Model Manager actions (live) ---
    // Open data/models/ in the OS file manager so the user can drop in GGUFs.
    Q_INVOKABLE void openModelsFolder();
    // Register a user-provided model file into the `models` table under `role`
    // (fast|heavy|vision|embedding). Validates the path exists; returns false and
    // posts a notice on bad input. Triggers a registry reload + models refresh.
    Q_INVOKABLE bool addModel(const QString& path, const QString& role);
    // Native file picker (C++ QFileDialog — no QtQuick.Dialogs deploy dep) then
    // addModel. Returns true if a file was chosen and registered.
    Q_INVOKABLE bool pickAndAddModel(const QString& role);
    // Reassign an existing model's role (backs the role ComboBox). Reloads the
    // registry and refreshes the Model Manager list.
    Q_INVOKABLE void setModelRole(const QString& id, const QString& role);
    // Persist that the first-run flow has been acknowledged (hides the first-run
    // opt-in banner from then on, even before any model is added).
    Q_INVOKABLE void completeFirstRun();

    // Wave Z: Memory dashboard (browse/search/delete long-term memories table).
    Q_INVOKABLE QVariantList listMemories(const QString& query = QString(), int limit = 100) const;
    Q_INVOKABLE bool deleteMemory(qint64 id);
    Q_INVOKABLE bool rememberNote(const QString& text, const QString& kind = QStringLiteral("note"));
    // Active household user for memory namespaces (-1 = none / shared).
    Q_INVOKABLE qint64 activeUserId() const { return active_user_id_; }
    Q_INVOKABLE void setActiveUserId(qint64 id);
    Q_INVOKABLE QVariantList listUsers() const;
    // Wave Z complete: create household user + trigger face enroll on vision.
    Q_INVOKABLE qint64 createUser(const QString& name);
    Q_INVOKABLE bool enrollUserFace(qint64 userId);
    // Opt-in update check (updates.enabled + updates.check_url).
    Q_INVOKABLE void checkForUpdates(bool quiet = false);

    // Dev/demo: publish a placeholder SurfaceRequest (bus → surfaceRequested).
    Q_INVOKABLE void spawnSurfaceDemo();

    // C1: human answer to a SafetyPolicy ConfirmRequest. Publishes ConfirmResponse
    // on the EventBus (AgentLoop resumes the parked goal). When alwaysAllow is
    // true (and approved), appends the tool name to safety.tool_overrides so
    // future calls auto-Allow (Deny still wins for path/cmd gates).
    Q_INVOKABLE void respondConfirm(const QString& id, bool approved,
                                    bool alwaysAllow = false);

signals:
    void listeningChanged();
    void activePersonalityChanged();
    void modelStatusChanged();
    void modelsChanged();      // model registry changed (add/role/load-state)
    void memoriesChanged();    // Wave Z Memory dashboard
    void activeUserChanged();
    void usersChanged();
    void updateAvailable(QString version, QString url, QString notes);
    void firstRunChanged();    // first-run state changed (model added / acknowledged)
    void assistantToken(QString request_id, QString text, bool done);  // -> chat view
    void noticePosted(QString level, QString source, QString message); // -> toasts/log
    void findObjectAnswered(QString query, QString answer);
    void vramChanged();
    void wakeWordPulse();
    // A3: SurfaceRequest flattened 1:1, including the extended spawn_surface
    // hints (caption/md/x/y/w/h/group — blank/-1 when not set). Appended after
    // the original 5 params, so existing QML handlers with fewer formal
    // parameters keep working unchanged.
    void surfaceRequested(QString id, QString action, QString type,
                          QString title, QString argsJson,
                          QString caption, QString md,
                          double x, double y, double w, double h, QString group);
    void goalUpdated(QString goalId, QString title, QString status, QString summary);
    // A3: ui_control open_page / window relays (QML handlers land in E4).
    void navigateRequested(QString page);
    void windowRequested(QString verb);
    // C1: SafetyPolicy ConfirmRequest flattened for QML (ConfirmDialog + toast).
    void confirmRequested(QString id, QString tool, QString summary,
                          QString argsPreview, QString reason);
    // C1: any path answered (dialog / notification / voice) — close the dialog.
    void confirmSettled(QString id);

private:
    void wireEventBus();
    void buildModels();      // construct the UI models + image provider
    void wireModels();       // connect EventBus -> models (queued onto UI thread)

    Database db_;

    // Wave-3 UI data layer. Parented to this AppController (UI thread) so Qt
    // delivers EventBus signals to them via queued connections automatically.
    std::unique_ptr<ChatModel>           chat_model_;
    std::unique_ptr<ShoppingModel>       shopping_model_;
    std::unique_ptr<CameraModel>         camera_model_;
    std::unique_ptr<TaskModel>           task_model_;
    std::unique_ptr<ScheduledGoalsModel> scheduled_goals_model_;
    std::unique_ptr<TimelineModel>       timeline_model_;
    std::unique_ptr<NotificationsModel>  notifications_model_;
    std::unique_ptr<SessionsModel>        sessions_model_;
    std::unique_ptr<SettingsController>  settings_;
    // E2: mirrors personality_->all() for the in-GUI editor. Holds a plain
    // reference to personality_ (constructed first in initialize(), destroyed
    // last in shutdown()'s teardown order below) — must be reset before
    // personality_ is.
    std::unique_ptr<PersonalityModel>    personality_model_;
    // The QML engine takes ownership of the image provider on registration; we
    // keep a non-owning pointer to push frames into it. Guarded for lifetime by
    // disconnecting the feed in shutdown().
    CameraImageProvider*                 image_provider_ = nullptr;

    // Process-wide settings facade. Promoted to a member (was a local in
    // initialize()) because GatewayService holds a Config& for its lifetime, so
    // it must outlive the gateway.
    std::unique_ptr<Config>            config_;

    std::unique_ptr<InferenceManager>  inference_;
    std::unique_ptr<TaskScheduler>     scheduler_;
    std::unique_ptr<ProactiveEngine>   proactive_;
    std::unique_ptr<IdleDetector>      idle_;
    std::unique_ptr<AudioService>      audio_;
    std::unique_ptr<VisionService>     vision_;
    std::unique_ptr<MemoryService>     memory_;
    std::unique_ptr<AgentRuntime>      agent_;
    std::unique_ptr<PersonalityManager> personality_;
    // C4: external agent CLI sessions (own QThread).
    std::unique_ptr<AgentSessionService> sessions_;

    // Mobile/web gateway: the IAssistantBridge adapter + the service it drives.
    // gateway_ runs on its own QThread (registered in threads_); bridge_ and
    // config_ must outlive it (torn down explicitly in shutdown(), gateway-first).
    std::unique_ptr<AppBridge>         bridge_;
    std::unique_ptr<GatewayService>    gateway_;

    std::vector<QThread*> threads_;

    bool    listening_ = false;
    QString active_personality_ = "Assistant";
    QString model_status_ = "no model loaded";
    int     vram_used_mib_ = 0;
    int     vram_total_mib_ = 0;
    // Wave Z: face-matched household user for memory namespaces (-1 = shared).
    qint64  active_user_id_ = -1;
    // C1: ConfirmRequest id → tool name (for always-allow override writes).
    QHash<QString, QString> pending_confirm_tools_;
};

} // namespace polymath
