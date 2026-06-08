#pragma once
//
// Persona — the active personality's runtime configuration, loaded from the DB
// (`personalities` row) + its bundle persona.json. Internal to src/agent.
//
#include "types.h"
#include <string>
#include <vector>

namespace polymath {

class Database;

struct Persona {
    std::string              name;
    std::string              system_prompt;
    std::string              voice;            // Piper voice id (for TTS)
    std::string              preferred_model;  // "fast" | "heavy" | a registry id
    std::vector<std::string> tools;            // allow-list ([] => all tools)
    SamplingParams           sampling;         // temperature/top_p from the bundle
    bool                     loaded = false;   // false => fell back to defaults
};

// Load the active personality (personalities WHERE is_active=1) and parse its
// bundle persona.json. On any failure returns a safe default persona (generic
// assistant prompt, all tools allowed) with loaded=false. Never throws.
Persona loadActivePersona(Database& db);

} // namespace polymath
