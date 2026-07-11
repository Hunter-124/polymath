#pragma once
//
// AgentLoop v2 — reusable plan/execute/reflect harness (overhaul 03 §2).
//
// Invoked from BOTH the interactive turn path (AgentRuntime) and the
// background scheduler. Runs on the calling worker thread; UI contact is via
// EventBus only (GoalUpdate, SurfaceRequest, tokens, speak, notices).
//
// Responsibilities:
//   - Turn router: quick | goal | command (grammar when a model is present;
//     deterministic heuristic otherwise / as fallback).
//   - Goal lifecycle: plan → execute → reflect, crash-resumable via goals /
//     plan_steps tables (schema from A3).
//   - Context assembly v2: token-budgeted system / memories / summary / recent
//     with correct Role labeling and memory injection.
//   - Terminal delivery via GoalUpdate (+ optional TTS for voice-origin goals).
//
#include "types.h"
#include "tool_registry.h"
#include "event_bus.h"

#include <nlohmann/json.hpp>
#include <QString>

#include <chrono>
#include <string>
#include <vector>

namespace polymath {

class Database;
class InferenceManager;
class TaskScheduler;
class MemoryService;
class TurnCollector;
struct Persona;

// Interactive-path classification (03 §2.1).
enum class TurnRoute { Quick, Goal, Command };

// One plan_steps row (in-memory).
struct PlanStepRec {
    int64_t        id = 0;
    int64_t        goal_id = 0;
    int            idx = 0;
    std::string    description;
    std::string    kind;          // tool | prompt | skill | agent_session | surface
    std::string    tool;
    nlohmann::json args = nlohmann::json::object();
    std::string    status = "pending";  // pending|running|done|failed|skipped
    nlohmann::json result;
    int            attempts = 0;
};

// One goals row + its steps.
struct GoalRec {
    int64_t                 id = 0;
    std::string             title;
    std::string             status = "active";
    std::string             origin = "chat";   // chat|voice|schedule|skill|agent
    nlohmann::json          context = nlohmann::json::object();
    nlohmann::json          result;
    std::vector<PlanStepRec> steps;
};

// Token budgets for n_ctx 4096 (03 §2.4). Public so tests can assert.
struct ContextBudgets {
    int system   = 1100;
    int memories = 400;
    int summary  = 400;
    int recent   = 1400;
    int reserve  = 700;   // generation + tool results
};

class AgentLoop {
public:
    AgentLoop(Database& db, InferenceManager& inf, TaskScheduler& sched,
              ToolRegistry& tools, MemoryService* memory, TurnCollector& collector);

    // CREATE IF NOT EXISTS conversation_summaries; running steps → pending.
    void recoverOnStartup();

    // Interactive turn: router → quick / goal / command. Streams final answer
    // under request_id when appropriate. Returns the answer text.
    std::string runInteractive(const std::string& user_text,
                               const QString& request_id,
                               bool from_voice);

    // Execute / resume a persisted goal (pending steps only). Terminal states
    // publish GoalUpdate. Safe to call without a loaded model when all steps
    // are pre-authored tool/surface steps.
    void executeGoal(int64_t goal_id);

    // Persist a goal + steps (no execution). Used by skills (C3), tests, and
    // the goal path after planning. Returns goal id.
    int64_t createGoal(const std::string& title,
                       const std::string& origin,
                       const nlohmann::json& context,
                       const std::vector<PlanStepRec>& steps);

    // Load goal + ordered steps from DB. steps empty if goal missing.
    GoalRec loadGoal(int64_t goal_id) const;

    // Context assembly v2 (token-budgeted, memory-injected, correct roles).
    std::vector<ChatMessage> assembleContext(
        const Persona& persona,
        const std::string& user_text,
        const nlohmann::json& tool_specs,
        const std::string& exclude_text = {},
        bool include_tool_protocol = true) const;

    // Deterministic 3-way classifier (no model). Also used as model fallback.
    // Router v2 (A1): trigger matching against loaded skills (word-boundary,
    // gaps allowed) + an intent-verb/media-object table, on top of the existing
    // Quick/Goal heuristics.
    static TurnRoute classifyRouteHeuristic(const std::string& user_text);

    // Final-answer hygiene (A1 / B-LEAK): normalize a model final answer that
    // may be (or contain) a tool-call JSON blob into user-facing prose. Pure and
    // static so tests can assert it directly; NEVER returns a string that begins
    // with '{'. Does NOT execute tools. `tool_digest` (optional) is used to
    // synthesize a short summary when no salvageable prose exists.
    static std::string sanitizeFinalText(const std::string& raw,
                                         const std::string& tool_digest = {});

    // Token helpers (public for deterministic budget tests).
    int         tokens(const std::string& text) const;
    std::string fitTokens(const std::string& text, int budget) const;
    std::string compactToolResult(const std::string& result_json) const;

    // Publish GoalUpdate + notice + optional speak for a terminal goal.
    void deliverGoalTerminal(const GoalRec& goal,
                             const std::string& summary,
                             bool from_voice) const;

    // Resume every active goal that still has pending work.
    void resumeActiveGoals();

    static ContextBudgets defaultBudgets() { return {}; }
    static constexpr int kMaxPlanSteps     = 12;
    static constexpr int kMaxTotalSteps    = 24;
    static constexpr int kMaxStepAttempts  = 3;
    static constexpr int kMaxQuickToolRounds = 2;
    static constexpr int kToolResultTokenCap = 1200;
    static constexpr int kStepTimeoutMs    = 120000;

private:
    // --- router -------------------------------------------------------------
    TurnRoute classifyRoute(const std::string& user_text,
                            const Persona& persona,
                            const QString& request_id);

    // --- paths --------------------------------------------------------------
    std::string runQuick(const std::string& user_text,
                         const Persona& persona,
                         const QString& request_id,
                         bool from_voice);
    std::string runGoalPath(const std::string& user_text,
                            const Persona& persona,
                            const QString& request_id,
                            bool from_voice);
    std::string runCommand(const std::string& user_text,
                           const Persona& persona,
                           const QString& request_id,
                           bool from_voice);

    // --- plan / reflect -----------------------------------------------------
    bool planGoal(GoalRec& goal, const std::string& user_text,
                  const Persona& persona, const QString& request_id);
    bool reflectAndReplan(GoalRec& goal, const PlanStepRec& failed,
                          const Persona& persona, const QString& request_id);

    // --- step dispatch ------------------------------------------------------
    bool executeStep(GoalRec& goal, PlanStepRec& step,
                     const Persona& persona, const QString& request_id);
    bool dispatchToolStep(GoalRec& goal, PlanStepRec& step,
                          const Persona& persona, const QString& request_id);
    bool dispatchPromptStep(GoalRec& goal, PlanStepRec& step,
                            const Persona& persona, const QString& request_id);
    bool dispatchSkillStep(GoalRec& goal, PlanStepRec& step);
    bool dispatchAgentSessionStep(GoalRec& goal, PlanStepRec& step);
    bool dispatchSurfaceStep(PlanStepRec& step);

    // --- persistence --------------------------------------------------------
    void ensureSummariesTable() const;
    void persistStep(const PlanStepRec& step) const;
    void persistGoalStatus(GoalRec& goal, const std::string& status,
                           const nlohmann::json& result = {}) const;
    void appendTrace(GoalRec& goal, const nlohmann::json& event) const;
    void insertSteps(int64_t goal_id, std::vector<PlanStepRec>& steps) const;
    void replacePendingSteps(GoalRec& goal,
                             const std::vector<PlanStepRec>& remaining) const;

    // --- context helpers ----------------------------------------------------
    std::string buildSystemPrompt(const Persona& persona,
                                  const nlohmann::json& tool_specs,
                                  bool include_tool_protocol) const;
    std::string loadRollingSummary() const;
    void        maybeUpdateRollingSummary(const std::string& exclude_text) const;
    std::vector<ChatMessage> loadRecentTurns(int token_budget,
                                             const std::string& exclude_text) const;
    std::string recallMemoriesBlock(const std::string& query, int token_budget) const;
    std::string modelIdFor(const Persona& persona) const;

    // --- generation helpers -------------------------------------------------
    std::string constrainedComplete(const std::vector<ChatMessage>& messages,
                                    const Persona& persona,
                                    const std::string& grammar,
                                    const QString& request_id,
                                    const QString& suffix,
                                    bool* ok = nullptr);
    std::string unconstrainedComplete(const std::vector<ChatMessage>& messages,
                                      const Persona& persona,
                                      const QString& request_id,
                                      bool stream,
                                      bool* ok = nullptr);
    // Generate + publish the user-visible final answer with the streaming guard
    // (A1 / B-LEAK): generates under a SHADOW request_id so raw tokens never
    // reach ChatModel, sniffs the leading chars, and only forwards clean prose
    // to the real request_id (post-processing any tool-call JSON first). Returns
    // the published prose.
    std::string finalizeAndPublishAnswer(std::vector<ChatMessage> clean_msgs,
                                         const Persona& persona,
                                         const QString& request_id,
                                         const std::string& tool_digest,
                                         const std::string& fallback,
                                         bool speak);
    // Turn a raw final answer into prose. Handles final_answer extraction,
    // one bonus tool round for another known tool, and JSON stripping. Never
    // returns a string beginning with '{'. `depth` bounds bonus-round recursion.
    std::string sanitizeFinalAnswer(const std::string& raw,
                                    std::vector<ChatMessage>& clean_msgs,
                                    const Persona& persona,
                                    const QString& request_id,
                                    const std::string& tool_digest,
                                    int depth);
    void streamOrPublishAnswer(const std::string& answer,
                               const Persona& persona,
                               const QString& request_id,
                               bool speak);
    void persistTranscript(const std::string& text, bool assistant) const;

    // --- wall-clock guard ---------------------------------------------------
    bool goalTimedOut(const std::chrono::steady_clock::time_point& start) const;
    int  goalTimeoutMin() const;

    Database&         db_;
    InferenceManager& inf_;
    TaskScheduler&    sched_;
    ToolRegistry&     tools_;
    MemoryService*    memory_ = nullptr;
    TurnCollector&    collector_;
};

// Free helpers shared with tests / runtime.
std::string turnRouteToString(TurnRoute r);
TurnRoute   turnRouteFromString(const std::string& s);

} // namespace polymath
