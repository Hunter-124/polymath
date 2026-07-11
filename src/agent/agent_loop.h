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
    // D2 goal-tree: 0 = root; join_policy applies when this goal is a parent
    // parked waiting_children (all | any | first_success).
    int64_t                 parent_id = 0;
    std::string             join_policy = "all";
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
    ~AgentLoop();

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
    // Non-const: a terminal child may resume its parent (D2 waiting_children).
    void deliverGoalTerminal(const GoalRec& goal,
                             const std::string& summary,
                             bool from_voice);

    // Resume every active goal that still has pending work (simple FIFO, one at
    // a time — a single inference thread executes goals serially). Also the
    // "scan for the next runnable goal after one finishes" hook.
    void resumeActiveGoals();

    // Single tool-invocation choke point for BOTH the goal and quick paths
    // (A2 §4). Resolves the tool, enforces the SafetyPolicy risk-gate (A4),
    // routes deep tasks to the scheduler, and invokes with exception safety.
    //   Allow   → invoke.
    //   Deny    → ToolResult error the model sees ("denied by safety policy: …").
    //   Confirm → ToolResult marker (content.confirm_required=true) WITHOUT
    //             invoking; the caller parks waiting_user and asks the human.
    ToolResult dispatchToolChecked(const std::string& tool,
                                   const nlohmann::json& args,
                                   ToolContext& ctx);

    // Session rejoin (A2 §3): a finished/failed external agent session resumes
    // the goal parked `waiting_agent` on it. `kind` is the AgentSessionEvent
    // kind ("Result" = success, "Error" = failure); `text` is the transcript
    // tail / result summary, injected as the parked step's result. No-op when no
    // goal waits on `session_id`. Safe to call without a model on the success
    // path (a failure path may run a reflect round if a model is present).
    void resumeForAgentSession(const std::string& session_id,
                               const std::string& kind,
                               const std::string& text);

    // Give up on any goal parked `waiting_agent` longer than
    // agents.join_timeout_min: inject a timeout result and run a reflect round
    // instead of hanging forever. Called periodically by the runtime.
    void sweepAgentJoinTimeouts();

    // A4: the human answered a SafetyPolicy ConfirmRequest. Approve → resume the
    // parked goal and run the pending call (bypassing the re-check for that one
    // call); deny → return the denial to the model. Wired to
    // EventBus::confirmResponse in the constructor (queued onto the worker
    // thread). The heavy resume runs under the runtime's busy-guard via
    // requestGoalExecution, so this never re-enters a live turn. Public so tests
    // and the confirm UI relay (node C1) can drive it.
    void onConfirmResponse(const ConfirmResponse& r);

    // D2: schema columns parent_id / join_policy on goals (idempotent ALTER).
    // Called from recoverOnStartup and by orchestration tools before insert.
    void ensureGoalTreeColumns() const;

    // D2: goal currently inside executeGoal on this thread (0 if none). Used by
    // spawn_subtask when the model omits parent_id.
    static int64_t executingGoalId();

    // D2 caps (hardcoded defaults; config keys agent.goal_tree_* optional later).
    static constexpr int kGoalTreeDepthMax  = 2;
    static constexpr int kGoalTreeChildCap  = 8;

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
    int  joinTimeoutMin() const;

    // --- session rejoin helpers ---------------------------------------------
    // Id of the goal parked waiting_agent on `session_id` (scans context_json),
    // or 0 if none.
    int64_t findGoalWaitingOnSession(const std::string& session_id) const;
    // Complete a parked goal: mark the parked step done|failed with `injected`
    // as its result, clear the waiting_* markers, set the goal active, then run
    // a reflect round (on failure) and continue executeGoal.
    void continueParkedGoal(int64_t goal_id, int step_idx, bool failed,
                            const nlohmann::json& injected,
                            const std::string& reason);

    // --- D2 goal-tree (waiting_children park / join / resume) ----------------
    struct ChildStats {
        int total = 0;
        int done = 0;      // status == done
        int failed = 0;    // status == failed | cancelled
        int active = 0;    // non-terminal (active | waiting_*)
    };
    ChildStats collectChildStats(int64_t parent_id) const;
    nlohmann::json buildChildrenDigest(int64_t parent_id) const;
    static bool joinPolicySatisfied(const std::string& policy, const ChildStats& s);
    static bool joinPolicySucceeded(const std::string& policy, const ChildStats& s);
    // Depth of goal_id in the tree (root = 0).
    int goalTreeDepth(int64_t goal_id) const;
    // If goal has non-terminal children: park waiting_children (or immediately
    // resume when join already satisfied). Returns true when the goal is left
    // parked or was fully resolved as a parent join (caller must not deliver a
    // plain terminal without children).
    bool maybeParkOrJoinChildren(GoalRec& goal);
    // Child reached a terminal status → if parent is waiting_children and join
    // policy is satisfied, resume the parent with a results digest.
    void tryResumeParentAfterChild(const GoalRec& child);

    // --- A4 risk-gate confirmation ------------------------------------------
    enum class ConfirmState { None, Approved, Denied };

    // CREATE IF NOT EXISTS pending_confirmations (durable so an approval after a
    // restart still resumes). Cheap + idempotent.
    void ensureConfirmTable() const;

    // Park a goal/step waiting_user for a Confirm ruling: persist the pending
    // call, publish a ConfirmRequest, set the goal waiting_user with resume
    // markers, and leave the step pending. Used by both the goal and quick paths
    // (the quick path first wraps the call in a one-step carrier goal).
    void parkForConfirmation(GoalRec& goal, PlanStepRec& step,
                             const std::string& tool, const nlohmann::json& args,
                             const ToolResult& gated, const QString& request_id);

    // Resume check at the top of dispatchToolStep: if the human already resolved
    // a confirmation for (goal_id, step_idx), consume the row and report the
    // decision so the step is invoked (bypassing the re-check) or failed.
    ConfirmState takeConfirmDecision(int64_t goal_id, int step_idx);

    // Resume an affirmative/negative reply typed in chat ("yes, do it" / "no")
    // that answers the most recent pending confirmation. Returns true (and fills
    // `answer`) when it handled the turn.
    bool maybeResumePendingConfirmation(const std::string& user_text,
                                        const Persona& persona,
                                        const QString& request_id,
                                        bool from_voice,
                                        std::string& answer);

    Database&         db_;
    InferenceManager& inf_;
    TaskScheduler&    sched_;
    ToolRegistry&     tools_;
    MemoryService*    memory_ = nullptr;
    TurnCollector&    collector_;

    // A4: the risk-gate (Config-backed, thread-safe, caches its compiled rules).
    // One instance so regexes/roots are compiled once, not per tool call.
    core::SafetyPolicy safety_;

    // A4: EventBus::confirmResponse subscription (queued onto the worker thread
    // via collector_ as context). Disconnected in ~AgentLoop.
    QMetaObject::Connection confirm_conn_;

    // D2: goal id of the executeGoal frame currently on this thread (0 = none).
    // Single agent worker thread → plain static is enough (not cross-thread).
    static int64_t s_executing_goal_id_;
};

// Free helpers shared with tests / runtime.
std::string turnRouteToString(TurnRoute r);
TurnRoute   turnRouteFromString(const std::string& s);

} // namespace polymath
