#pragma once
//
// ITool — a capability the LLM can invoke via function calling.  Concrete tools
// live in agent/tools/*.  The AgentRuntime builds a per-turn tool list (filtered
// by the active personality's allow-list), constrains the model to emit valid
// JSON arguments (GBNF derived from `schema`), then dispatches invoke().
//
#include <nlohmann/json.hpp>
#include <string>

namespace polymath {

class InferenceManager;   // fwd
class Database;           // fwd

// Services a tool may reach.  Passed by reference; do not store beyond invoke().
struct ToolContext {
    InferenceManager* inference = nullptr;
    Database*         db = nullptr;
    int64_t           active_user_id = -1;
    std::string       active_personality;
};

struct ToolResult {
    bool            ok = true;
    nlohmann::json  content;     // structured result fed back to the model
    std::string     summary;     // short human-readable line for the activity log
};

struct ITool {
    virtual ~ITool() = default;

    // Stable name used in the tool-call protocol (snake_case, e.g. "web_search").
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;

    // JSON Schema for the arguments object (OpenAI-style "parameters").
    virtual nlohmann::json parametersSchema() const = 0;

    // Some tools are heavy/slow (research, report gen) and should be queued as a
    // deep task rather than run inline. The runtime checks this before invoking.
    virtual bool isDeepTask() const { return false; }

    virtual ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) = 0;
};

} // namespace polymath
