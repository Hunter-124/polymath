#pragma once
//
// grammar — build a GBNF grammar that constrains llama.cpp output to a single
// JSON tool-call object, given a list of tool JSON-schemas.
//
// The agent runtime offers a per-turn allow-list of tools (ITool::name() +
// ITool::parametersSchema()).  We translate that allow-list into a GBNF that
// forces the model to emit exactly:
//
//   { "tool": "<one_of_the_names>", "arguments": { ...schema-shaped... } }
//
// The grammar is consumed by LlamaBackend via llama_sampler_init_grammar(); it
// is also reachable directly when an upstream component (AgentRuntime) wants to
// precompute the constraint and stash it in ChatRequest.sampling.grammar.
//
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace polymath::grammar {

// One offered tool: its protocol name and its JSON-Schema "parameters" object
// (OpenAI-style — the same json ITool::parametersSchema() returns).
struct ToolDef {
    std::string    name;
    nlohmann::json parameters;   // JSON Schema object ({"type":"object",...})
};

// Build a GBNF that constrains output to a single tool-call object of the form
// {"tool":"<name>","arguments":{...}}.  When more than one tool is supplied the
// root alternates per-tool so each branch pins the correct argument shape.
// Returns "" when `tools` is empty (caller should then sample unconstrained).
std::string buildToolCallGrammar(const std::vector<ToolDef>& tools);

// Build a GBNF that accepts any syntactically-valid JSON value.  Useful as a
// fallback (e.g. structured output without a fixed schema).
std::string jsonValueGrammar();

// Lower-level: translate a single JSON Schema node into a GBNF rule body. The
// generated helper rules are appended to `out_rules` (deduplicated by name);
// the function returns the rule-reference to splice into a parent rule.
// Exposed for testing and reuse; most callers want buildToolCallGrammar().
std::string schemaToGbnf(const nlohmann::json& schema,
                         std::vector<std::string>& out_rules,
                         const std::string& rule_prefix);

} // namespace polymath::grammar
