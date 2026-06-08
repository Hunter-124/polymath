#pragma once
//
// AgentRuntime — drives the LLM<->tools loop for one conversation turn:
//   1. Build messages (persona system prompt + history + user turn).
//   2. Offer the allowed tools; constrain output to valid tool-call JSON (GBNF).
//   3. If the model calls a tool: run it (inline) or queue it (isDeepTask) via
//      the scheduler, feed the result back, and loop until a final answer.
//   4. Stream the final answer's tokens to the UI and to TTS.
//
#include "service.h"
#include "tool_registry.h"
#include "types.h"
#include <QObject>

namespace polymath {

class Database;
class InferenceManager;
class TaskScheduler;

class AgentRuntime : public QObject, public IService {
    Q_OBJECT
public:
    AgentRuntime(Database& db, InferenceManager& inf, TaskScheduler& sched,
                 QObject* parent = nullptr);

    void start() override;
    void stop() override;
    const char* serviceName() const override { return "agent"; }

    ToolRegistry& tools() { return registry_; }

public slots:
    // Entry points. Both run the agentic loop on this service's worker thread.
    void handleUtterance(const polymath::Utterance& u);   // from ASR
    void handleTextInput(const QString& text, const QString& request_id); // from chat UI

signals:
    void turnStarted(QString request_id);
    void turnFinished(QString request_id, QString final_text);

private:
    Database&         db_;
    InferenceManager& inf_;
    TaskScheduler&    sched_;
    ToolRegistry      registry_;
};

} // namespace polymath
