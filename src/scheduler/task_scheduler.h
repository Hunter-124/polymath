#pragma once
//
// TaskScheduler — persistent priority queue of "deep-work" jobs (lab reports,
// research, daily summaries, batch image analysis).  Real-time voice bypasses
// it.  When the IdleDetector reports the machine quiet, it asks the
// InferenceManager to load the Heavy model, drains the queue, then releases it.
//
#include "service.h"
#include <nlohmann/json.hpp>
#include <QObject>

namespace polymath {

class Database;
class InferenceManager;

class TaskScheduler : public QObject, public IService {
    Q_OBJECT
public:
    TaskScheduler(Database& db, InferenceManager& inf, QObject* parent = nullptr);

    void start() override;
    void stop() override;
    const char* serviceName() const override { return "scheduler"; }

    // Push a deep task. Returns the task id (also persisted in `tasks`).
    qint64 enqueue(const std::string& type, const nlohmann::json& params, int priority = 0);
    void   cancel(qint64 task_id);

public slots:
    void onIdleChanged(bool idle);   // wired from IdleDetector

signals:
    void taskFinished(qint64 task_id, QString result_json);

private:
    Database&         db_;
    InferenceManager& inf_;
    bool              idle_ = false;
};

} // namespace polymath
