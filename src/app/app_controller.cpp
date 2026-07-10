#include "app_controller.h"
#include "event_bus.h"
#include "config.h"
#include "paths.h"
#include "logging.h"
#include "service.h"

#include "inference_manager.h"
#include "task_scheduler.h"
#include "proactive_engine.h"
#include "audio_service.h"
#include "vision_service.h"
#include "memory_service.h"
#include "agent_runtime.h"
#include "tool_registry.h"
#include "personality_manager.h"

#include "chat_model.h"
#include "shopping_model.h"
#include "camera_model.h"
#include "task_model.h"
#include "timeline_model.h"
#include "notifications_model.h"
#include "sessions_model.h"
#include "settings_controller.h"
#include "camera_image_provider.h"
#include "agent_session_service.h"

#include "app_bridge.h"
#include "gateway_service.h"

#include <QByteArray>
#include <QDesktopServices>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

#include <filesystem>
#include <nlohmann/json.hpp>

namespace polymath {

AppController::AppController(QObject* parent) : QObject(parent) {}

AppController::~AppController() { shutdown(); }

bool AppController::initialize() {
    Paths::instance().ensureLayout();
    logging::init(Paths::instance().logs().string());

    // At-rest encryption key: a per-install random secret kept in a DPAPI-
    // protected keyfile next to the DB (local-only, never hardcoded). If the
    // linked SQLite has no SQLCipher codec, open() reports encryptionActive()
    // == false and the DB stays plaintext; we still pass the key so a future
    // codec-enabled build (and the first-run plaintext->encrypted migration)
    // engages automatically. If a key cannot be created we degrade to an
    // unencrypted open rather than refusing to start.
    const std::string keyfile = (Paths::instance().root() / "db.key").string();
    std::string key = Database::loadOrCreateKey(keyfile);
    if (key.empty())
        PM_WARN("AppController: no install key available — opening DB unencrypted.");
    if (!db_.open(Paths::instance().db().string(), key)) {
        PM_ERROR("failed to open database");
        return false;
    }
    if (!key.empty() && !db_.encryptionActive())
        PM_WARN("AppController: DB key supplied but at-rest encryption is INACTIVE "
                "(no SQLCipher codec in this build).");
    config_ = std::make_unique<Config>(db_);
    config_->seedDefaults();

    // --- construct services (dependency order) ---
    inference_   = std::make_unique<InferenceManager>(db_);
    scheduler_   = std::make_unique<TaskScheduler>(db_, *inference_);
    proactive_   = std::make_unique<ProactiveEngine>(db_);
    idle_        = std::make_unique<IdleDetector>();
    memory_      = std::make_unique<MemoryService>(db_, *inference_);
    agent_       = std::make_unique<AgentRuntime>(db_, *inference_, *scheduler_, memory_.get());
    vision_      = std::make_unique<VisionService>(db_, *inference_);
    audio_       = std::make_unique<AudioService>(db_);
    personality_ = std::make_unique<PersonalityManager>(db_);
    // C4: external agent sessions (Claude Code / Codex / PTY). Needs Config.
    sessions_    = std::make_unique<AgentSessionService>(db_, *config_);
    // C5: late-bind agent_* tools to the live sessions service (no hard link
    // from register_tools → pm_sessions; QObject* Q_INVOKABLE dispatch).
    setAgentSessionService(sessions_.get());

    // --- UI data models (own thread = UI thread; parented to this) ---
    buildModels();

    wireEventBus();
    wireModels();

    // --- run long-lived services each on their own thread ---
    threads_.push_back(runOnThread(inference_.get(), inference_.get()));
    threads_.push_back(runOnThread(scheduler_.get(), scheduler_.get()));
    threads_.push_back(runOnThread(proactive_.get(), proactive_.get()));
    threads_.push_back(runOnThread(idle_.get(), idle_.get()));
    threads_.push_back(runOnThread(memory_.get(), memory_.get()));
    threads_.push_back(runOnThread(agent_.get(), agent_.get()));
    threads_.push_back(runOnThread(vision_.get(), vision_.get()));
    threads_.push_back(runOnThread(audio_.get(), audio_.get()));
    threads_.push_back(runOnThread(sessions_.get(), sessions_.get()));
    personality_->start();   // lightweight: stays on the UI thread

    // --- mobile/web gateway (LAN HTTP+WS, optional relay tunnel) ---
    // The bridge forwards the narrow IAssistantBridge surface to this controller.
    // The service lives on the UI thread (like personality_): its QHttpServer /
    // QWebSocketServer only need *an* event loop, and keeping it here lets QML
    // bind the gateway's signals + Q_INVOKABLEs for the Mobile Access pairing UI
    // (QML refuses to connect to a QObject owned by another thread). Request
    // handling is light (DB reads + bridge calls that marshal to workers). It
    // self-creates its `devices` table + gateway.* settings and only dials the
    // relay once remote access is explicitly enabled.
    bridge_  = std::make_unique<AppBridge>(*this);
    gateway_ = std::make_unique<GatewayService>(*bridge_, db_, *config_);
    gateway_->start();

    PM_INFO("AppController initialized");
    return true;
}

QObject* AppController::gateway() const { return gateway_.get(); }

void AppController::wireEventBus() {
    auto& bus = EventBus::instance();

    // Backend -> UI
    connect(&bus, &EventBus::tokenStreamed, this, [this](const TokenChunk& t) {
        emit assistantToken(t.request_id, t.text, t.done);
    });
    connect(&bus, &EventBus::notice, this, [this](const Notice& n) {
        emit noticePosted(n.level, n.source, n.message);
    });
    connect(&bus, &EventBus::findObjectDone, this, [this](const FindObjectResult& r) {
        emit findObjectAnswered(r.query, r.answer);
    });

    // ASR -> agent ; agent speak -> TTS  (worker-to-worker, queued automatically)
    connect(&bus, &EventBus::utterance, agent_.get(), &AgentRuntime::handleUtterance);
    connect(&bus, &EventBus::speakRequested, audio_.get(),
            [this](const SpeakRequest& s) { QMetaObject::invokeMethod(audio_.get(), "speak",
                Qt::QueuedConnection, Q_ARG(QString, s.text), Q_ARG(QString, s.voice)); });

    // Idle detector -> scheduler
    connect(idle_.get(), &IdleDetector::idleChanged, scheduler_.get(), &TaskScheduler::onIdleChanged);

    // A3: taskFinished → notification + chat delivery (was orphaned; results
    // previously died in tasks.result_json). Mirrors §2.3 goal-delivery path.
    connect(scheduler_.get(), &TaskScheduler::taskFinished, this,
            [this](qint64 task_id, const QString& result_json) {
                QString type = QStringLiteral("task");
                QString summary;
                bool ok = true;
                try {
                    const auto j = nlohmann::json::parse(result_json.toStdString());
                    if (j.contains("type") && j["type"].is_string())
                        type = QString::fromStdString(j["type"].get<std::string>());
                    if (j.contains("summary") && j["summary"].is_string())
                        summary = QString::fromStdString(j["summary"].get<std::string>());
                    else if (j.contains("text") && j["text"].is_string())
                        summary = QString::fromStdString(j["text"].get<std::string>());
                    if (j.contains("ok") && j["ok"].is_boolean())
                        ok = j["ok"].get<bool>();
                    if (j.contains("error")) ok = false;
                } catch (...) {
                    summary = result_json;
                }
                if (summary.size() > 240)
                    summary = summary.left(237) + QStringLiteral("...");

                const QString title = type;
                const QString body = summary.isEmpty()
                    ? QStringLiteral("Task %1 finished").arg(task_id)
                    : summary;
                const QString level = ok ? QStringLiteral("good") : QStringLiteral("error");
                EventBus::instance().publishNotice(
                    {level, QStringLiteral("scheduler"),
                     QStringLiteral("✔ Finished: %1 — %2").arg(title, body)});

                // Inject an assistant chat line so the user sees the result
                // even when not looking at the task queue / notification center.
                if (chat_model_) {
                    const QString rid = QStringLiteral("task-done-%1").arg(task_id);
                    const QString msg = QStringLiteral("✔ Finished: %1 — %2").arg(title, body);
                    chat_model_->appendAssistantToken(rid, msg, /*done*/ false);
                    chat_model_->appendAssistantToken(rid, QString(), /*done*/ true);
                }
            });

    // Audio listening state -> UI property
    connect(audio_.get(), &AudioService::listeningStateChanged, this, [this](bool on) {
        listening_ = on; emit listeningChanged();
    });

    // Personality switch -> UI property
    connect(personality_.get(), &PersonalityManager::activeChanged, this,
            [this](const QString& name, const QString&) {
                active_personality_ = name; emit activePersonalityChanged();
            });

    // Model status -> UI property. A load/unload also means the registry-derived
    // hasModels/firstRun state may have changed (e.g. the Fast model became
    // resident at startup, or a freshly added model loaded), so re-publish those.
    connect(inference_.get(), &InferenceManager::modelStateChanged, this,
            [this](const QString& role, const QString& id, bool loaded) {
                model_status_ = loaded ? (role + ": " + id) : QStringLiteral("no model loaded");
                emit modelStatusChanged();
                emit modelsChanged();
                emit firstRunChanged();
            });

    // VRAM HUD (first consumer of InferenceManager::vramChanged).
    connect(inference_.get(), &InferenceManager::vramChanged, this,
            [this](int used, int total) {
                vram_used_mib_ = used;
                vram_total_mib_ = total;
                emit vramChanged();
            });

    // Wake-word pulse for dashboard HUD.
    connect(audio_.get(), &AudioService::wakeWordHeard, this, [this]() {
        emit wakeWordPulse();
    });

    // SurfaceHost / goal delivery relays (bus → QML-friendly signals).
    connect(&bus, &EventBus::surfaceRequested, this,
            [this](const SurfaceRequest& r) {
                emit surfaceRequested(r.id, r.action, r.type, r.title, r.args_json);
            });
    connect(&bus, &EventBus::goalUpdated, this,
            [this](const GoalUpdate& g) {
                emit goalUpdated(g.goal_id, g.title, g.status, g.summary);
            });
}

// --- Wave-3 UI data layer ------------------------------------------------

void AppController::buildModels() {
    chat_model_     = std::make_unique<ChatModel>(this);
    shopping_model_ = std::make_unique<ShoppingModel>(db_, this);
    camera_model_   = std::make_unique<CameraModel>(db_, this);
    task_model_     = std::make_unique<TaskModel>(db_, this);
    timeline_model_ = std::make_unique<TimelineModel>(db_, this);
    notifications_model_ = std::make_unique<NotificationsModel>(db_, this);
    sessions_model_ = std::make_unique<SessionsModel>(db_, this);
    if (sessions_)
        sessions_model_->setService(sessions_.get());
    image_provider_ = new CameraImageProvider();   // engine takes ownership later

    // SettingsController needs Config (seeded in initialize before buildModels).
    if (config_)
        settings_ = std::make_unique<SettingsController>(db_, *config_, this);

    // Initial population from SQLite (tables are the source of truth).
    shopping_model_->refresh();
    camera_model_->refresh();
    task_model_->refresh();
    timeline_model_->refresh();
    notifications_model_->refreshFromEvents();
    // sessions_model_ refreshes after the service thread has ensureSchema'd;
    // a deferred refresh is triggered from wireModels once the bus is live.
}

void AppController::wireModels() {
    auto& bus = EventBus::instance();

    // Assistant tokens -> ChatModel (coalesced per request_id). Queued onto the
    // UI thread because the bus emits from the inference worker.
    connect(&bus, &EventBus::tokenStreamed, chat_model_.get(),
            [this](const TokenChunk& t) {
                chat_model_->appendAssistantToken(t.request_id, t.text, t.done);
            });

    // Live frames have two consumers:
    //  1) the image provider's byte cache — fed directly on the worker thread
    //     (updateFrame is mutex-guarded and cheap, so no UI hop needed); and
    //  2) the CameraModel's live/tick flags — must touch the model on the UI
    //     thread, so that connection stays auto/queued.
    connect(&bus, &EventBus::frameReady, this,
            [this](const Frame& f) {
                if (image_provider_) {
                    QByteArray jpeg(reinterpret_cast<const char*>(f.jpeg.data()),
                                    static_cast<int>(f.jpeg.size()));
                    image_provider_->updateFrame(f.camera_id, jpeg);
                }
            }, Qt::DirectConnection);
    connect(&bus, &EventBus::frameReady, camera_model_.get(), &CameraModel::onFrame);

    // Task queue updates -> TaskModel (queued from scheduler worker).
    connect(&bus, &EventBus::taskUpdated, task_model_.get(), &TaskModel::onTaskUpdated);

    // Timeline live feed: detections + utterances (queued from vision/audio).
    connect(&bus, &EventBus::detection, timeline_model_.get(), &TimelineModel::onDetection);
    connect(&bus, &EventBus::utterance, timeline_model_.get(), &TimelineModel::onUtterance);

    // Memory summaries / new memories land in the DB on the memory worker; the
    // "memory" notice is our cue to refresh the timeline so they appear.
    connect(&bus, &EventBus::notice, timeline_model_.get(), [this](const Notice& n) {
        if (n.source == QLatin1String("memory")) timeline_model_->refresh();
    });

    // Notifications center: bus consumers (independent of toast chain).
    if (notifications_model_) {
        connect(&bus, &EventBus::notice, notifications_model_.get(),
                &NotificationsModel::onNotice);
        connect(&bus, &EventBus::taskUpdated, notifications_model_.get(),
                &NotificationsModel::onTask);
        connect(&bus, &EventBus::reminderFired, notifications_model_.get(),
                &NotificationsModel::onReminder);
        connect(&bus, &EventBus::detection, notifications_model_.get(),
                &NotificationsModel::onDetection);
        connect(&bus, &EventBus::goalUpdated, notifications_model_.get(),
                &NotificationsModel::onGoalUpdate);
    }

    // C4: external agent sessions → SessionsModel (queued onto UI thread).
    if (sessions_model_) {
        connect(&bus, &EventBus::agentSessionEvent, sessions_model_.get(),
                &SessionsModel::onAgentSessionEvent);
    }
}

void AppController::registerWithEngine(QQmlApplicationEngine& engine) {
    // Image provider: "image://cameras/<id>". The engine takes ownership.
    if (image_provider_)
        engine.addImageProvider(QStringLiteral("cameras"), image_provider_);

    // Expose models as context properties as well as via the app.* Q_PROPERTYs,
    // so views may bind either way.
    QQmlContext* ctx = engine.rootContext();
    ctx->setContextProperty("chatModel",     chat_model_.get());
    ctx->setContextProperty("shoppingModel", shopping_model_.get());
    ctx->setContextProperty("cameraModel",   camera_model_.get());
    ctx->setContextProperty("taskModel",     task_model_.get());
    ctx->setContextProperty("timelineModel", timeline_model_.get());
    // Mobile gateway pairing helpers for Settings ▸ Mobile Access (remote toggle,
    // pairing payload/QR, connected-device count). Its Q_INVOKABLEs are
    // thread-safe (auth pair-code store is mutex-guarded; the relay toggle
    // marshals onto the gateway thread).
    ctx->setContextProperty("gateway",       gateway_.get());
    // Overhaul A2 facades.
    ctx->setContextProperty("settings",      settings_.get());
    ctx->setContextProperty("notifications", notifications_model_.get());
    // C4: external agent session cards.
    ctx->setContextProperty("agentSessions", sessions_model_.get());
    // Real app enables MultiEffect glass; capture_views forces false.
    ctx->setContextProperty("pmEffectsEnabled", true);
}

QObject* AppController::chatModel() const     { return chat_model_.get(); }
QObject* AppController::shoppingModel() const { return shopping_model_.get(); }
QObject* AppController::cameraModel() const   { return camera_model_.get(); }
QObject* AppController::taskModel() const     { return task_model_.get(); }
QObject* AppController::timelineModel() const { return timeline_model_.get(); }

void AppController::shutdown() {
    // Stop feeding the (engine-owned) image provider and detach the bus from the
    // UI models before we tear anything down, so no late frame/token from a
    // still-draining worker thread reaches a half-destroyed receiver.
    auto& bus = EventBus::instance();
    if (chat_model_)     disconnect(&bus, nullptr, chat_model_.get(),     nullptr);
    if (camera_model_)   disconnect(&bus, nullptr, camera_model_.get(),   nullptr);
    if (task_model_)     disconnect(&bus, nullptr, task_model_.get(),     nullptr);
    if (timeline_model_) disconnect(&bus, nullptr, timeline_model_.get(), nullptr);
    if (sessions_model_) disconnect(&bus, nullptr, sessions_model_.get(), nullptr);
    // The frame-feed lambda is owned by `this` as the connection context.
    disconnect(&bus, &EventBus::frameReady, this, nullptr);
    image_provider_ = nullptr;   // ownership belongs to the QML engine

    // Stop the gateway first (it lives on this UI thread): close its listener and
    // relay so no mobile request is handled while the workers it bridges to are
    // being torn down below.
    if (gateway_) gateway_->stop();

    for (auto* t : threads_) { if (t) { t->quit(); t->wait(2000); } }
    threads_.clear();

    // Drop the UI models (parented to this, but reset explicitly for order).
    chat_model_.reset(); shopping_model_.reset(); camera_model_.reset();
    task_model_.reset(); timeline_model_.reset();
    sessions_model_.reset(); notifications_model_.reset(); settings_.reset();

    // Gateway first (its thread is already joined above): it holds refs to the
    // bridge, db_ and config_, so it must die before them.
    gateway_.reset(); bridge_.reset();

    inference_.reset(); scheduler_.reset(); proactive_.reset(); idle_.reset();
    memory_.reset(); agent_.reset(); vision_.reset(); audio_.reset(); personality_.reset();
    sessions_.reset();
    config_.reset();   // outlived the gateway; safe to drop before the DB closes
    db_.close();
}

// --- QML actions ---------------------------------------------------------

void AppController::sendText(const QString& text) {
    const QString rid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QMetaObject::invokeMethod(agent_.get(), "handleTextInput", Qt::QueuedConnection,
                              Q_ARG(QString, text), Q_ARG(QString, rid));
}

void AppController::pushToTalk(bool down) {
    QMetaObject::invokeMethod(audio_.get(), "pushToTalk", Qt::QueuedConnection, Q_ARG(bool, down));
}

void AppController::setPersonality(const QString& name) { personality_->setActive(name.toStdString()); }

QStringList AppController::personalities() const {
    QStringList out;
    for (const auto& p : personality_->all()) out << QString::fromStdString(p.name);
    return out;
}

void AppController::setPrivacy(const QString& key, bool enabled) {
    db_.setSetting(key.toStdString(), enabled ? "1" : "0");
    auto& bus = EventBus::instance();
    bus.publishPrivacy({key, enabled});

    // When the master kill-switch flips, every per-feature sense's *effective*
    // state changes too (Config gates each sense behind the master). Re-emit each
    // sense key's effective value (newMaster AND its own raw toggle) so already-
    // running services tear down / restore live capture immediately, instead of
    // only on their next independent toggle change.
    if (key == QString::fromUtf8(keys::MasterEnabled)) {
        for (const char* sense : { keys::MicEnabled, keys::AmbientTranscription,
                                   keys::FaceRecognition, keys::CamerasEnabled }) {
            const bool effective = enabled && db_.getBool(std::string(sense), false);
            bus.publishPrivacy({ QString::fromUtf8(sense), effective });
        }
    }
}

bool AppController::privacy(const QString& key) const {
    return const_cast<Database&>(db_).getBool(key.toStdString(), true);
}

void AppController::findObject(const QString& query) {
    QMetaObject::invokeMethod(vision_.get(), "findObject", Qt::QueuedConnection, Q_ARG(QString, query));
}

void AppController::addShoppingItem(const QString& item) {
    // Route through the model so the DB write and the UI list stay in sync (the
    // model performs the INSERT). Falls back to a direct insert if the model is
    // not yet built (e.g. called before initialize()).
    if (shopping_model_) {
        shopping_model_->addItem(item);
    } else {
        db_.exec("INSERT INTO shopping_items(item,created_at) VALUES(?1,?2)",
                 {item.toStdString(), to_unix(Clock::now())});
    }
}

// --- Wave-3 UI invokables ------------------------------------------------

void AppController::refreshAll() {
    refreshShopping();
    refreshCameras();
    refreshTasks();
    refreshTimeline();
}

void AppController::refreshShopping() { if (shopping_model_) shopping_model_->refresh(); }
void AppController::refreshCameras()  { if (camera_model_)   camera_model_->refresh(); }
void AppController::refreshTasks()    { if (task_model_)     task_model_->refresh(); }
void AppController::refreshTimeline() { if (timeline_model_) timeline_model_->refresh(); }

void AppController::sendChat(const QString& text) {
    submitChatTurn(text);
}

QString AppController::submitChatTurn(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return {};
    // Append the user turn to the chat model on the UI thread. AutoConnection:
    // direct when called from the UI/QML path, queued when the gateway worker
    // thread calls us (so the QAbstractListModel is only ever touched on its own
    // thread).
    QMetaObject::invokeMethod(this, [this, t] { if (chat_model_) chat_model_->appendUser(t); });
    const QString rid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QMetaObject::invokeMethod(agent_.get(), "handleTextInput", Qt::QueuedConnection,
                              Q_ARG(QString, t), Q_ARG(QString, rid));
    return rid;
}

QVariantList AppController::models() const {
    QVariantList out;
    const_cast<Database&>(db_).query(
        "SELECT id,display_name,role,path,n_ctx,n_gpu_layers,is_active "
        "FROM models ORDER BY role ASC, display_name ASC",
        {},
        [&](const Row& r) {
            QVariantMap m;
            m["id"]          = QString::fromStdString(r.text(0));
            m["displayName"] = QString::fromStdString(r.text(1));
            m["role"]        = QString::fromStdString(r.text(2));
            m["path"]        = QString::fromStdString(r.text(3));
            m["nCtx"]        = static_cast<int>(r.i64(4));
            m["nGpuLayers"]  = static_cast<int>(r.i64(5));
            m["active"]      = r.i64(6) != 0;
            out.push_back(m);
        });
    return out;
}

// --- First-run / Model Manager actions -----------------------------------

bool AppController::hasModels() const {
    // "Usable" = a registered model whose file still exists on disk. We check the
    // path so a stale row (model file deleted) does not keep the cold-start banner
    // hidden. Cheap bounded read on the UI thread.
    namespace fs = std::filesystem;
    bool usable = false;
    const_cast<Database&>(db_).query(
        "SELECT path FROM models", {},
        [&](const Row& r) {
            if (usable) return;
            std::error_code ec;
            const std::string p = r.text(0);
            if (!p.empty() && fs::exists(fs::u8path(p), ec)) usable = true;
        });
    return usable;
}

bool AppController::firstRun() const {
    // First run until the user has a usable model OR has explicitly acknowledged
    // the first-run flow. Once acknowledged it stays acknowledged across restarts.
    if (const_cast<Database&>(db_).getBool(keys::FirstRunDone, false)) return false;
    return !hasModels();
}

void AppController::openModelsFolder() {
    namespace fs = std::filesystem;
    const fs::path dir = Paths::instance().models();
    std::error_code ec;
    fs::create_directories(dir, ec);   // make sure it exists so the open succeeds
    const QString local = QString::fromStdString(dir.string());
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(local))) {
        emit noticePosted(QStringLiteral("warn"), QStringLiteral("models"),
                          QStringLiteral("Could not open the models folder: %1").arg(local));
    }
}

bool AppController::addModel(const QString& path, const QString& role) {
    const QFileInfo fi(path);
    if (path.isEmpty() || !fi.exists() || !fi.isFile()) {
        emit noticePosted(QStringLiteral("warn"), QStringLiteral("models"),
                          QStringLiteral("Cannot add model — file not found: %1").arg(path));
        return false;
    }

    // Normalize role to the schema's allowed set; default to "fast".
    QString r = role.toLower();
    if (r != "fast" && r != "heavy" && r != "vision" && r != "embedding")
        r = QStringLiteral("fast");

    const std::string p  = fi.absoluteFilePath().toStdString();
    const std::string id = fi.completeBaseName().toStdString();   // stem, e.g. "gemma-3n"
    const std::string rs = r.toStdString();

    // Skip if this exact path is already registered (mirrors auto-discover).
    bool exists = false;
    db_.query("SELECT 1 FROM models WHERE path=?1", {p}, [&](const Row&) { exists = true; });
    if (exists) {
        emit noticePosted(QStringLiteral("info"), QStringLiteral("models"),
                          QStringLiteral("Model already registered: %1").arg(path));
    } else {
        // Insert the row the same way auto-register does (id == display_name, all
        // GPU layers requested; is_active=1 only if no other model holds this role
        // yet). n_ctx mirrors auto-discover's per-role default.
        const int n_ctx = (rs == "embedding") ? 2048 : 8192;
        db_.exec(
            "INSERT INTO models(id,display_name,path,role,n_ctx,n_gpu_layers,"
            "chat_template,mmproj_path,is_active) VALUES(?1,?1,?2,?3,?4,999,'','',"
            "(SELECT CASE WHEN EXISTS(SELECT 1 FROM models WHERE role=?3) THEN 0 ELSE 1 END))",
            {id, p, rs, n_ctx});
        emit noticePosted(QStringLiteral("info"), QStringLiteral("models"),
                          QStringLiteral("Registered %1 as %2").arg(fi.fileName(), r));
    }

    // Pick up the new row in the running InferenceManager and refresh the UI.
    // reloadRegistry() is a plain method (not a slot), so dispatch it as a queued
    // functor onto the inference thread rather than by method name.
    if (auto* inf = inference_.get())
        QMetaObject::invokeMethod(inf, [inf]() { inf->reloadRegistry(); }, Qt::QueuedConnection);
    emit modelsChanged();
    emit firstRunChanged();
    return true;
}

void AppController::setModelRole(const QString& id, const QString& role) {
    QString r = role.toLower();
    if (r != "fast" && r != "heavy" && r != "vision" && r != "embedding") {
        emit noticePosted(QStringLiteral("warn"), QStringLiteral("models"),
                          QStringLiteral("Unknown model role: %1").arg(role));
        return;
    }
    bool exists = false;
    db_.query("SELECT 1 FROM models WHERE id=?1", {id.toStdString()}, [&](const Row&) { exists = true; });
    if (!exists) {
        emit noticePosted(QStringLiteral("warn"), QStringLiteral("models"),
                          QStringLiteral("No such model: %1").arg(id));
        return;
    }
    db_.exec("UPDATE models SET role=?2 WHERE id=?1", {id.toStdString(), r.toStdString()});

    if (auto* inf = inference_.get())
        QMetaObject::invokeMethod(inf, [inf]() { inf->reloadRegistry(); }, Qt::QueuedConnection);
    emit modelsChanged();
}

void AppController::completeFirstRun() {
    db_.setSetting(std::string(keys::FirstRunDone), "1");
    emit firstRunChanged();
}

void AppController::spawnSurfaceDemo() {
    SurfaceRequest r;
    r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.action = QStringLiteral("spawn");
    r.type = QStringLiteral("placeholder");
    r.title = QStringLiteral("Demo surface");
    r.args_json = QStringLiteral("{\"note\":\"spawnSurfaceDemo\"}");
    EventBus::instance().publishSurfaceRequest(r);
}

} // namespace polymath
