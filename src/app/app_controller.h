#pragma once
//
// AppController — the single QObject facade exposed to QML (context property
// "app").  It owns the Database, constructs every backend service, moves each
// onto its own QThread, wires them together through the EventBus, and surfaces
// state/actions to the UI.  The Wave-3 UI agent extends this with the
// QAbstractListModels (chat, shopping, cameras, tasks, timeline).
//
#include "database.h"
#include <QObject>
#include <QStringList>
#include <memory>
#include <vector>

class QThread;

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

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(QString activePersonality READ activePersonality NOTIFY activePersonalityChanged)
    Q_PROPERTY(QString modelStatus READ modelStatus NOTIFY modelStatusChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    bool initialize();    // open DB, seed config, build + start services
    void shutdown();

    bool    listening() const { return listening_; }
    QString activePersonality() const { return active_personality_; }
    QString modelStatus() const { return model_status_; }

    // --- QML-callable actions ---
    Q_INVOKABLE void sendText(const QString& text);
    Q_INVOKABLE void pushToTalk(bool down);
    Q_INVOKABLE void setPersonality(const QString& name);
    Q_INVOKABLE QStringList personalities() const;
    Q_INVOKABLE void setPrivacy(const QString& key, bool enabled);
    Q_INVOKABLE bool privacy(const QString& key) const;
    Q_INVOKABLE void findObject(const QString& query);
    Q_INVOKABLE void addShoppingItem(const QString& item);

signals:
    void listeningChanged();
    void activePersonalityChanged();
    void modelStatusChanged();
    void assistantToken(QString request_id, QString text, bool done);  // -> chat view
    void noticePosted(QString level, QString source, QString message); // -> toasts/log
    void findObjectAnswered(QString query, QString answer);

private:
    void wireEventBus();

    Database db_;

    std::unique_ptr<InferenceManager>  inference_;
    std::unique_ptr<TaskScheduler>     scheduler_;
    std::unique_ptr<ProactiveEngine>   proactive_;
    std::unique_ptr<IdleDetector>      idle_;
    std::unique_ptr<AudioService>      audio_;
    std::unique_ptr<VisionService>     vision_;
    std::unique_ptr<MemoryService>     memory_;
    std::unique_ptr<AgentRuntime>      agent_;
    std::unique_ptr<PersonalityManager> personality_;

    std::vector<QThread*> threads_;

    bool    listening_ = false;
    QString active_personality_ = "Assistant";
    QString model_status_ = "no model loaded";
};

} // namespace polymath
