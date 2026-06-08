#include "grammar.h"
#include "logging.h"

#include <algorithm>
#include <cctype>
#include <sstream>

// GBNF reference: ggml-org/llama.cpp grammars/README.md (assumed llama.cpp
// release tag ~b4000+, the post-`llama_sampler` API). The grammar text produced
// here is fed to llama_sampler_init_grammar(vocab, grammar_str, "root").
//
// Design notes:
//   * We keep the grammar deliberately permissive on whitespace and lenient on
//     value types (numbers/strings/bools/null/arrays/objects) so a slightly
//     wandering model can still satisfy it, while pinning the *structure*:
//     a top-level {"tool": "<name>", "arguments": {...}} object.
//   * Property ordering inside "arguments" is NOT enforced (models reorder keys
//     freely); we accept any subset/superset-free permutation by emitting an
//     object whose members are an unordered alternation of the known keys. This
//     mirrors how llama.cpp's own json-schema-to-grammar handles `properties`
//     without `additionalProperties:false` strictness.

namespace polymath::grammar {

namespace {

// Primitive terminals shared by every generated grammar. Mirrors the canonical
// JSON grammar shipped with llama.cpp (grammars/json.gbnf), trimmed.
constexpr const char* kPrimitives = R"GBNF(
ws    ::= [ \t\n]*
string ::= "\"" ( [^"\\] | "\\" (["\\/bfnrt] | "u" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F]) )* "\""
number ::= "-"? ([0-9] | [1-9] [0-9]*) ("." [0-9]+)? ([eE] [-+]? [0-9]+)?
boolean ::= "true" | "false"
null   ::= "null"
value  ::= object | array | string | number | boolean | null
array  ::= "[" ws ( value (ws "," ws value)* )? ws "]"
object ::= "{" ws ( string ws ":" ws value (ws "," ws string ws ":" ws value)* )? ws "}"
)GBNF";

// A safe GBNF rule-name fragment derived from an arbitrary key/name.
std::string sanitize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(c));
        else out += '-';
    }
    if (out.empty()) out = "x";
    if (std::isdigit(static_cast<unsigned char>(out.front()))) out = "x-" + out;
    return out;
}

// Emit a GBNF string-literal terminal matching exactly `literal` (escaped).
std::string quotedLiteral(const std::string& literal) {
    std::string out = "\"\\\"";   // opening: \"
    for (char c : literal) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:   out += c;      break;
        }
    }
    out += "\\\"\"";              // closing: \"
    return out;
}

bool ruleExists(const std::vector<std::string>& rules, const std::string& name) {
    const std::string needle = name + " ::=";
    return std::any_of(rules.begin(), rules.end(), [&](const std::string& r) {
        return r.rfind(needle, 0) == 0;
    });
}

} // namespace

std::string schemaToGbnf(const nlohmann::json& schema,
                         std::vector<std::string>& out_rules,
                         const std::string& rule_prefix) {
    // Map a JSON-Schema node to a reference to a (possibly newly-emitted) rule.
    if (!schema.is_object()) return "value";

    // enum -> alternation of literals.
    if (schema.contains("enum") && schema["enum"].is_array() && !schema["enum"].empty()) {
        const std::string name = rule_prefix + "-enum";
        if (!ruleExists(out_rules, name)) {
            std::ostringstream rule;
            rule << name << " ::= ";
            bool first = true;
            for (const auto& e : schema["enum"]) {
                if (!first) rule << " | ";
                first = false;
                if (e.is_string()) rule << quotedLiteral(e.get<std::string>());
                else               rule << "\"" << e.dump() << "\"";
            }
            out_rules.push_back(rule.str());
        }
        return name;
    }

    const std::string type = schema.value("type", std::string("value"));

    if (type == "string")  return "string";
    if (type == "integer" || type == "number") return "number";
    if (type == "boolean") return "boolean";
    if (type == "null")    return "null";

    if (type == "array") {
        const std::string name = rule_prefix + "-arr";
        std::string itemRef = "value";
        if (schema.contains("items"))
            itemRef = schemaToGbnf(schema["items"], out_rules, rule_prefix + "-item");
        if (!ruleExists(out_rules, name)) {
            std::ostringstream rule;
            rule << name << " ::= \"[\" ws ( " << itemRef
                 << " (ws \",\" ws " << itemRef << ")* )? ws \"]\"";
            out_rules.push_back(rule.str());
        }
        return name;
    }

    if (type == "object") {
        const std::string name = rule_prefix + "-obj";
        if (ruleExists(out_rules, name)) return name;

        const auto props = schema.value("properties", nlohmann::json::object());
        if (props.empty()) {
            // Unknown shape: accept any JSON object.
            out_rules.push_back(name + " ::= object");
            return name;
        }

        // Build one "key : value" member rule per declared property, then allow
        // those members in any order (unordered alternation, comma-separated).
        std::vector<std::string> memberRefs;
        for (auto it = props.begin(); it != props.end(); ++it) {
            const std::string key = it.key();
            const std::string memberName = name + "-" + sanitize(key);
            const std::string valRef =
                schemaToGbnf(it.value(), out_rules, memberName);
            if (!ruleExists(out_rules, memberName)) {
                std::ostringstream rule;
                rule << memberName << " ::= " << quotedLiteral(key)
                     << " ws \":\" ws " << valRef;
                out_rules.push_back(rule.str());
            }
            memberRefs.push_back(memberName);
        }

        std::ostringstream anyMember;
        anyMember << name << "-member ::= ";
        for (size_t i = 0; i < memberRefs.size(); ++i) {
            if (i) anyMember << " | ";
            anyMember << memberRefs[i];
        }
        out_rules.push_back(anyMember.str());

        std::ostringstream rule;
        rule << name << " ::= \"{\" ws ( " << name << "-member"
             << " (ws \",\" ws " << name << "-member)* )? ws \"}\"";
        out_rules.push_back(rule.str());
        return name;
    }

    // No/unknown type -> any JSON value.
    return "value";
}

std::string buildToolCallGrammar(const std::vector<ToolDef>& tools) {
    if (tools.empty()) return {};

    std::vector<std::string> rules;
    std::vector<std::string> rootBranches;

    for (size_t i = 0; i < tools.size(); ++i) {
        const auto& tool = tools[i];
        const std::string prefix = "tool-" + sanitize(tool.name) +
                                   "-" + std::to_string(i);
        const std::string argsRef =
            schemaToGbnf(tool.parameters.is_object() ? tool.parameters
                                                     : nlohmann::json::object(),
                         rules, prefix + "-args");

        std::ostringstream branch;
        branch << prefix << " ::= \"{\" ws "
               << quotedLiteral("tool")  << " ws \":\" ws " << quotedLiteral(tool.name)
               << " ws \",\" ws "
               << quotedLiteral("arguments") << " ws \":\" ws " << argsRef
               << " ws \"}\"";
        rules.push_back(branch.str());
        rootBranches.push_back(prefix);
    }

    std::ostringstream out;
    std::ostringstream root;
    root << "root ::= ws (";
    for (size_t i = 0; i < rootBranches.size(); ++i) {
        if (i) root << " | ";
        root << rootBranches[i];
    }
    root << ") ws";
    out << root.str() << "\n";
    for (const auto& r : rules) out << r << "\n";
    out << kPrimitives;

    PM_DEBUG("built tool-call GBNF for {} tool(s), {} bytes",
             tools.size(), out.str().size());
    return out.str();
}

std::string jsonValueGrammar() {
    std::ostringstream out;
    out << "root ::= ws value ws\n" << kPrimitives;
    return out.str();
}

} // namespace polymath::grammar
