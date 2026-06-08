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
#include "event_bus.h"   // also provides types.h + Q_DECLARE_METATYPE for the
                          // payload types used in this class's slots (Utterance)
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace polymath {

class Database;
class InferenceManager;
class TaskScheduler;
class TurnCollector;
struct Persona;

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
    // Core loop (runs on the agent worker thread; blocks here, never the UI).
    void runTurn(const std::string& user_text, const QString& request_id, bool from_voice);

    // Build the system message: persona prompt + tool-use protocol + tool catalog.
    std::string buildSystemPrompt(const Persona& persona,
                                  const nlohmann::json& tool_specs) const;

    // Final unconstrained generation: stream the answer under request_id, persist
    // + speak it, and emit turnFinished. `fallback` is replayed if streaming fails.
    void streamFinalAnswer(std::vector<ChatMessage>& messages,
                           const Persona& persona,
                           const QString& request_id,
                           const std::string& fallback);

    // Pull the last few command-turn transcripts as prior conversation context.
    // `exclude_text`, if non-empty, drops an exact-match latest row (the current
    // voice utterance, which AudioService persists before the agent runs).
    std::vector<ChatMessage> recentHistory(int max_turns,
                                           const std::string& exclude_text) const;

    // Persist a line into `transcripts` (command turns only; ambient is audio's).
    void persistTranscript(const std::string& text, bool assistant) const;

    Database&                      db_;
    InferenceManager&              inf_;
    TaskScheduler&                 sched_;
    ToolRegistry                   registry_;
    std::unique_ptr<TurnCollector> collector_;
    // One turn at a time: tool dispatch spins a nested event loop (Qt Network),
    // so a queued second turn must not interleave with one in flight.
    std::atomic<bool>              busy_{false};
};

} // namespace polymath
