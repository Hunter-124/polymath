#include "persona.h"
#include "database.h"
#include "paths.h"
#include "logging.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace polymath {

namespace {

Persona defaultPersona() {
    Persona p;
    p.name = "Assistant";
    p.system_prompt =
        "You are Hearth, a helpful, concise local home assistant. Use the available "
        "tools when they help you answer or act on the user's behalf.";
    p.preferred_model = "fast";
    p.sampling.temperature = 0.7f;
    p.sampling.top_p = 0.9f;
    p.loaded = false;
    return p;
}

// Resolve a (possibly relative) bundle path against the app root.
std::filesystem::path resolveBundle(const std::string& bundle_path) {
    std::filesystem::path p(bundle_path);
    if (p.is_absolute()) return p;
    return Paths::instance().root() / p;
}

} // namespace

Persona loadActivePersona(Database& db) {
    Persona persona = defaultPersona();

    // 1) Active personality row (name, bundle_path, voice, preferred_model).
    std::string bundlePath;
    std::string rowVoice, rowModel;
    bool found = false;
    db.query("SELECT name,bundle_path,voice,preferred_model FROM personalities "
             "WHERE is_active=1 LIMIT 1",
             {}, [&](const Row& r) {
                 persona.name    = r.text(0);
                 bundlePath      = r.text(1);
                 rowVoice        = r.text(2);
                 rowModel        = r.text(3);
                 found = true;
             });

    if (!found) {
        PM_WARN("persona: no active personality; using default");
        return persona;
    }
    // DB values are sensible defaults even if the bundle is missing/partial.
    if (!rowVoice.empty()) persona.voice = rowVoice;
    if (!rowModel.empty()) persona.preferred_model = rowModel;

    // 2) Parse the bundle persona.json for prompt / sampling / tools / voice.
    const std::filesystem::path bundle = resolveBundle(bundlePath);
    std::ifstream f(bundle);
    if (!f) {
        PM_WARN("persona: cannot open bundle '{}'; using DB row + defaults", bundle.string());
        persona.loaded = true;   // we still have a real personality from the DB
        return persona;
    }

    std::stringstream ss;
    ss << f.rdbuf();
    nlohmann::json j = nlohmann::json::parse(ss.str(), nullptr, /*allow_exceptions*/ false);
    if (j.is_discarded() || !j.is_object()) {
        PM_ERROR("persona: malformed JSON in '{}'; using DB row + defaults", bundle.string());
        persona.loaded = true;
        return persona;
    }

    if (j.contains("name") && j["name"].is_string())
        persona.name = j["name"].get<std::string>();
    if (j.contains("system_prompt") && j["system_prompt"].is_string())
        persona.system_prompt = j["system_prompt"].get<std::string>();
    if (j.contains("voice") && j["voice"].is_string() && !j["voice"].get<std::string>().empty())
        persona.voice = j["voice"].get<std::string>();
    if (j.contains("preferred_model") && j["preferred_model"].is_string())
        persona.preferred_model = j["preferred_model"].get<std::string>();

    if (j.contains("tools") && j["tools"].is_array()) {
        persona.tools.clear();
        for (const auto& t : j["tools"])
            if (t.is_string()) persona.tools.push_back(t.get<std::string>());
    }

    if (j.contains("sampling") && j["sampling"].is_object()) {
        const auto& s = j["sampling"];
        if (s.contains("temperature") && s["temperature"].is_number())
            persona.sampling.temperature = s["temperature"].get<float>();
        if (s.contains("top_p") && s["top_p"].is_number())
            persona.sampling.top_p = s["top_p"].get<float>();
        if (s.contains("top_k") && s["top_k"].is_number())
            persona.sampling.top_k = s["top_k"].get<int>();
        if (s.contains("max_tokens") && s["max_tokens"].is_number())
            persona.sampling.max_tokens = s["max_tokens"].get<int>();
        if (s.contains("repeat_penalty") && s["repeat_penalty"].is_number())
            persona.sampling.repeat_penalty = s["repeat_penalty"].get<float>();
    }

    persona.loaded = true;
    PM_INFO("persona: loaded '{}' ({} tool(s) allowed, voice='{}')",
            persona.name, persona.tools.size(), persona.voice);
    return persona;
}

} // namespace polymath
