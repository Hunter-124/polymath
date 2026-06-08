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
#include "personality_manager.h"

#include <QThread>
#include <QUuid>

namespace polymath {

AppController::AppController(QObject* parent) : QObject(parent) {}

AppController::~AppController() { shutdown(); }

bool AppController::initialize() {
    Paths::instance().ensureLayout();
    logging::init(Paths::instance().logs().string());

    std::string key;
    // (Encryption key would be derived from the OS credential store when enabled.)
    if (!db_.open(Paths::instance().db().string(), key)) {
        PM_ERROR("failed to open database");
        return false;
    }
    Config cfg(db_);
    cfg.seedDefaults();

    // --- construct services (dependency order) ---
    inference_   = std::make_unique<InferenceManager>(db_);
    scheduler_   = std::make_unique<TaskScheduler>(db_, *inference_);
    proactive_   = std::make_unique<ProactiveEngine>(db_);
    idle_        = std::make_unique<IdleDetector>();
    memory_      = std::make_unique<MemoryService>(db_, *inference_);
    agent_       = std::make_unique<AgentRuntime>(db_, *inference_, *scheduler_);
    vision_      = std::make_unique<VisionService>(db_, *inference_);
    audio_       = std::make_unique<AudioService>(db_);
    personality_ = std::make_unique<PersonalityManager>(db_);

    wireEventBus();

    // --- run long-lived services each on their own thread ---
    threads_.push_back(runOnThread(inference_.get(), inference_.get()));
    threads_.push_back(runOnThread(scheduler_.get(), scheduler_.get()));
    threads_.push_back(runOnThread(proactive_.get(), proactive_.get()));
    threads_.push_back(runOnThread(idle_.get(), idle_.get()));
    threads_.push_back(runOnThread(memory_.get(), memory_.get()));
    threads_.push_back(runOnThread(agent_.get(), agent_.get()));
    threads_.push_back(runOnThread(vision_.get(), vision_.get()));
    threads_.push_back(runOnThread(audio_.get(), audio_.get()));
    personality_->start();   // lightweight: stays on the UI thread

    PM_INFO("AppController initialized");
    return true;
}

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

    // Audio listening state -> UI property
    connect(audio_.get(), &AudioService::listeningStateChanged, this, [this](bool on) {
        listening_ = on; emit listeningChanged();
    });

    // Personality switch -> UI property
    connect(personality_.get(), &PersonalityManager::activeChanged, this,
            [this](const QString& name, const QString&) {
                active_personality_ = name; emit activePersonalityChanged();
            });

    // Model status -> UI property
    connect(inference_.get(), &InferenceManager::modelStateChanged, this,
            [this](const QString& role, const QString& id, bool loaded) {
                model_status_ = loaded ? (role + ": " + id) : QStringLiteral("no model loaded");
                emit modelStatusChanged();
            });
}

void AppController::shutdown() {
    for (auto* t : threads_) { if (t) { t->quit(); t->wait(2000); } }
    threads_.clear();
    inference_.reset(); scheduler_.reset(); proactive_.reset(); idle_.reset();
    memory_.reset(); agent_.reset(); vision_.reset(); audio_.reset(); personality_.reset();
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
    EventBus::instance().publishPrivacy({key, enabled});
}

bool AppController::privacy(const QString& key) const {
    return const_cast<Database&>(db_).getBool(key.toStdString(), true);
}

void AppController::findObject(const QString& query) {
    QMetaObject::invokeMethod(vision_.get(), "findObject", Qt::QueuedConnection, Q_ARG(QString, query));
}

void AppController::addShoppingItem(const QString& item) {
    db_.exec("INSERT INTO shopping_items(item,created_at) VALUES(?1,?2)",
             {item.toStdString(), to_unix(Clock::now())});
}

} // namespace polymath
