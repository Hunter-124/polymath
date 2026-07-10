#include "skill.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace polymath {

namespace {

const std::unordered_set<std::string> kStepKinds = {
    "tool", "prompt", "skill", "agent_session", "surface",
};

// Filesystem-safe skill names: [a-z0-9_]+ with at least one char.
bool isSafeName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (unsigned char c : name) {
        if (!(std::islower(c) || std::isdigit(c) || c == '_')) return false;
    }
    return true;
}

std::string jsonToSubstString(const nlohmann::json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    if (v.is_number_float()) {
        std::ostringstream oss;
        oss << v.get<double>();
        return oss.str();
    }
    if (v.is_null()) return {};
    return v.dump();
}

// Collect required param property names from a JSON Schema-ish params object.
std::vector<std::string> requiredParams(const nlohmann::json& params_schema) {
    std::vector<std::string> out;
    if (!params_schema.is_object()) return out;
    if (params_schema.contains("required") && params_schema["required"].is_array()) {
        for (const auto& r : params_schema["required"]) {
            if (r.is_string()) out.push_back(r.get<std::string>());
        }
    }
    return out;
}

} // namespace

bool isValidSkillStepKind(const std::string& kind) {
    return kStepKinds.count(kind) > 0;
}

SkillValidation validateSkillJson(const nlohmann::json& j, const std::string& fallback_name) {
    SkillValidation v;
    if (!j.is_object()) {
        v.error = "skill root must be a JSON object";
        return v;
    }

    Skill s;
    s.name = j.value("name", fallback_name);
    if (s.name.empty()) {
        v.error = "skill name is required";
        return v;
    }
    if (!isSafeName(s.name)) {
        v.error = "skill name must be snake_case [a-z0-9_] (got '" + s.name + "')";
        return v;
    }

    s.description = j.value("description", std::string{});
    if (s.description.empty()) {
        v.error = "skill description is required";
        return v;
    }

    if (j.contains("triggers")) {
        if (!j["triggers"].is_array()) {
            v.error = "triggers must be an array of strings";
            return v;
        }
        for (const auto& t : j["triggers"]) {
            if (!t.is_string()) {
                v.error = "triggers entries must be strings";
                return v;
            }
            s.triggers.push_back(t.get<std::string>());
        }
    }

    if (j.contains("params")) {
        if (!j["params"].is_object()) {
            v.error = "params must be a JSON object (JSON Schema)";
            return v;
        }
        s.params = j["params"];
    } else {
        s.params = {{"type", "object"}, {"properties", nlohmann::json::object()}};
    }

    s.confirm = j.value("confirm", false);

    if (!j.contains("steps") || !j["steps"].is_array() || j["steps"].empty()) {
        v.error = "steps must be a non-empty array";
        return v;
    }

    for (size_t i = 0; i < j["steps"].size(); ++i) {
        const auto& sj = j["steps"][i];
        if (!sj.is_object()) {
            v.error = "steps[" + std::to_string(i) + "] must be an object";
            return v;
        }
        SkillStep step;
        step.kind = sj.value("kind", std::string{});
        if (!isValidSkillStepKind(step.kind)) {
            v.error = "steps[" + std::to_string(i) + "]: invalid kind '" + step.kind + "'";
            return v;
        }
        step.tool = sj.value("tool", std::string{});
        step.description = sj.value("description", std::string{});
        if (sj.contains("args")) {
            if (!sj["args"].is_object()) {
                v.error = "steps[" + std::to_string(i) + "].args must be an object";
                return v;
            }
            step.args = sj["args"];
        }
        if (step.kind == "tool" && step.tool.empty()) {
            v.error = "steps[" + std::to_string(i) + "]: kind=tool requires tool name";
            return v;
        }
        if (step.description.empty()) {
            // Synthesize a default description so plan_steps.description NOT NULL is satisfied.
            if (step.kind == "tool")
                step.description = "Run tool " + step.tool;
            else
                step.description = step.kind + " step";
        }
        s.steps.push_back(std::move(step));
    }

    v.ok = true;
    v.skill = std::move(s);
    return v;
}

SkillValidation loadSkillFile(const std::filesystem::path& path) {
    SkillValidation v;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        v.error = "file not found: " + path.string();
        return v;
    }
    try {
        std::ifstream in(path);
        if (!in) {
            v.error = "cannot open: " + path.string();
            return v;
        }
        nlohmann::json j = nlohmann::json::parse(in);
        const std::string fallback = path.parent_path().filename().string();
        v = validateSkillJson(j, fallback);
        if (v.ok) v.skill.source_path = path;
        return v;
    } catch (const nlohmann::json::exception& e) {
        v.error = std::string("JSON parse error: ") + e.what();
        return v;
    } catch (const std::exception& e) {
        v.error = std::string("load error: ") + e.what();
        return v;
    }
}

std::string substituteParams(const std::string& text, const nlohmann::json& params) {
    if (text.empty() || text.find('{') == std::string::npos) return text;
    if (!params.is_object()) return text;

    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ) {
        if (text[i] == '{') {
            // Find matching '}' for a simple {identifier} token.
            size_t j = i + 1;
            while (j < text.size() &&
                   (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) {
                ++j;
            }
            if (j < text.size() && text[j] == '}' && j > i + 1) {
                const std::string key = text.substr(i + 1, j - i - 1);
                if (params.contains(key)) {
                    out += jsonToSubstString(params[key]);
                    i = j + 1;
                    continue;
                }
            }
            // Unresolved or not a param token — copy literally.
            out += text[i];
            ++i;
        } else {
            out += text[i];
            ++i;
        }
    }
    return out;
}

nlohmann::json substituteParamsJson(const nlohmann::json& value,
                                    const nlohmann::json& params) {
    if (value.is_string()) {
        return substituteParams(value.get<std::string>(), params);
    }
    if (value.is_array()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& el : value)
            arr.push_back(substituteParamsJson(el, params));
        return arr;
    }
    if (value.is_object()) {
        nlohmann::json obj = nlohmann::json::object();
        for (auto it = value.begin(); it != value.end(); ++it)
            obj[it.key()] = substituteParamsJson(it.value(), params);
        return obj;
    }
    return value;
}

nlohmann::json expandSkillToGoal(const Skill& skill, const nlohmann::json& params_in) {
    nlohmann::json params = params_in.is_object() ? params_in : nlohmann::json::object();

    // Required-param check against the skill's params schema.
    for (const auto& req : requiredParams(skill.params)) {
        if (!params.contains(req) || params[req].is_null() ||
            (params[req].is_string() && params[req].get<std::string>().empty())) {
            return {{"error", "missing required param: " + req}};
        }
    }

    nlohmann::json steps = nlohmann::json::array();
    for (const auto& s : skill.steps) {
        nlohmann::json step = {
            {"kind", s.kind},
            {"description", substituteParams(s.description, params)},
            {"args", substituteParamsJson(s.args, params)},
        };
        if (!s.tool.empty())
            step["tool"] = substituteParams(s.tool, params);
        steps.push_back(std::move(step));
    }

    const std::string title = skill.description.empty()
        ? ("Skill: " + skill.name)
        : substituteParams(skill.description, params);

    return {
        {"title", title},
        {"origin", "skill"},
        {"context", {
            {"skill", skill.name},
            {"params", params},
            {"confirm", skill.confirm},
        }},
        {"steps", std::move(steps)},
        {"confirm", skill.confirm},
    };
}

nlohmann::json skillCatalogEntry(const Skill& skill) {
    return {
        {"name", skill.name},
        {"description", skill.description},
        {"triggers", skill.triggers},
        {"params", skill.params},
        {"confirm", skill.confirm},
        {"step_count", static_cast<int>(skill.steps.size())},
    };
}

} // namespace polymath
