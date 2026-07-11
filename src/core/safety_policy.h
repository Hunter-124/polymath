#pragma once
//
// SafetyPolicy (A4) — the central risk-gate for every tool invocation.
//
// AgentLoop routes ALL tool calls through one choke point
// (AgentLoop::dispatchToolChecked). Before invoking, the loop asks this policy
// for a Ruling; the loop then Allows (invoke), Denies (return a tool error the
// model sees), or Confirms (park the goal/turn waiting_user and ask the human).
//
// Lives in pm_core with NO dependency on pm_agent: the agent's ToolRiskClass is
// mapped to the mirror enum `core::RiskLevel` at the call site (see
// tool_registry.h::toRiskLevel). The policy is thread-safe (a mutex guards a
// small compiled cache) and Config-backed (all knobs are `safety.*` settings so
// the Settings ▸ Safety UI — node C1 — can change them at runtime).
//
#include <nlohmann/json.hpp>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

namespace polymath {

class Database;

namespace core {

// Mirror of pm_agent's ToolRiskClass (kept in core so pm_core does not link
// pm_agent). The agent maps ToolRiskClass -> RiskLevel at the dispatch site.
// Ordering is the auto-allow rank: Read < WriteLocal < External < Spend <
// Destructive.
enum class RiskLevel {
    Read = 0,
    WriteLocal = 1,
    External = 2,
    Spend = 3,
    Destructive = 4,
};

enum class Decision { Allow, Confirm, Deny };

inline const char* decisionName(Decision d) {
    switch (d) {
    case Decision::Allow:   return "allow";
    case Decision::Confirm: return "confirm";
    case Decision::Deny:    return "deny";
    }
    return "allow";
}

struct Ruling {
    Decision    decision = Decision::Allow;
    std::string reason;      // human-readable; shown to the model / in the audit log
};

// Parse a RiskLevel from a config token ("read"|"write_local"|"external"|
// "spend"|"destructive"). Unknown -> WriteLocal (the standard base).
RiskLevel riskLevelFromString(const std::string& s);
const char* riskLevelName(RiskLevel r);

class SafetyPolicy {
public:
    explicit SafetyPolicy(Database& db) : db_(db) {}

    // The single decision entry point. `tool` is the tool name (for the reason
    // text), `risk` its mirrored risk level, `args` the JSON arguments. Pure
    // w.r.t. side effects — never invokes anything. Thread-safe.
    Ruling check(const std::string& tool, RiskLevel risk,
                 const nlohmann::json& args) const;

    // safety.audit (default on): whether the loop should record every gated
    // invocation to the ActivityLog.
    bool auditEnabled() const;

    // A short, human-facing one-liner describing what `tool`/`args` would do,
    // for the ConfirmRequest summary + the "⚠ Needs your approval" chat line.
    static std::string describe(const std::string& tool, const nlohmann::json& args);

    // A compact, safe preview of the args for the confirm dialog (node C1).
    static std::string argsPreview(const nlohmann::json& args);

private:
    struct Compiled {
        std::string             mode;                 // strict|standard|trusted
        RiskLevel               autoconfirm_max = RiskLevel::WriteLocal;
        std::vector<std::string> allowed_roots;       // normalized (lower, '/')
        std::vector<std::regex> denied_globs;
        std::vector<std::regex> cmd_denylist;
        // C1: tools the user permanently auto-allowed (safety.tool_overrides).
        // Exact tool-name match; Deny (path/cmd/write-cap) still wins.
        std::unordered_set<std::string> tool_overrides;
        long long               max_write_bytes = 2048LL * 1024;
        // Raw config strings this Compiled was built from (cache invalidation).
        std::string raw_mode, raw_autoconfirm, raw_roots, raw_agent_dirs,
                    raw_globs, raw_cmds, raw_maxkb, raw_tool_overrides;
    };

    // Rebuilds `cache_` from Config if any underlying key changed. Caller holds mtx_.
    void refresh() const;

    Database&                 db_;
    mutable std::mutex        mtx_;
    mutable Compiled          cache_;
    mutable bool              built_ = false;
};

} // namespace core
} // namespace polymath
