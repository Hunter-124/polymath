#include "config.h"
#include "database.h"
#include <array>
#include <cstring>
#include <utility>

namespace polymath {

void Config::seedDefaults() {
    // {key, default}. Privacy defaults ON (product decision: configurable, default-on).
    // Size is deduced from the initializers — do NOT hard-code a count (a
    // mismatch leaves {nullptr,nullptr} pairs that crash on use).
    static const std::pair<const char*, const char*> defaults[] = {
        {keys::MasterEnabled,        "1"},
        {keys::MicEnabled,           "1"},
        {keys::AmbientTranscription, "1"},
        {keys::FaceRecognition,      "1"},
        {keys::CamerasEnabled,       "1"},
        {keys::EncryptAtRest,        "0"},
        {keys::RetainAmbientDays,    "7"},
        {keys::RetainEventsDays,     "30"},
        {keys::QuietHoursStart,      "22:00"},
        {keys::QuietHoursEnd,        "07:00"},
        {keys::WakeWord,             "hey_jarvis"},
        {keys::SearchBackend,        "ddg"},
        {keys::SearchApiKey,         ""},
        {keys::FirstRunDone,         "0"},
        // Overhaul A2 defaults (docs/overhaul/02 + 04).
        {keys::UiAccent,             "#33E1FF"},
        {keys::UiEffects,            "1"},
        {keys::UiEffectsIntensity,   "0.6"},
        {keys::UiFontScale,          "1.0"},
        {keys::UiReduceMotion,       "0"},
        {keys::AudioInputDevice,     ""},
        {keys::AudioOutputDevice,    ""},
        {keys::AudioAsrIdleUnloadS,  "90"},
        {keys::AppStartMinimized,    "0"},
        {keys::AppLaunchOnLogin,     "0"},
        {keys::AgentsAllowedDirs,    ""},
        {keys::AgentsMaxConcurrent,  "2"},
        {keys::AgentsSpeakNeedsInput,"1"},
        {keys::LlmKvQuant,           "q8_0"},
        {keys::AgentGoalTimeoutMin,  "30"},
        {keys::AgentSpeakResults,    "1"},
    };
    // INSERT OR IGNORE seeds only missing keys (never clobbers user changes),
    // since settings.key is the PRIMARY KEY.
    for (const auto& [k, v] : defaults)
        db_.exec("INSERT OR IGNORE INTO settings(key,value) VALUES(?1,?2)",
                 {std::string(k), std::string(v)});
}

bool Config::isMasterGated(const char* key) {
    // The per-feature sense toggles that actually drive capture. The master
    // switch, the encryption flag, retention windows and behaviour keys are NOT
    // gated (turning the master off must not, e.g., disable encryption).
    if (!key) return false;
    return std::strcmp(key, keys::MicEnabled) == 0
        || std::strcmp(key, keys::AmbientTranscription) == 0
        || std::strcmp(key, keys::FaceRecognition) == 0
        || std::strcmp(key, keys::CamerasEnabled) == 0;
}

bool Config::masterEnabled() const {
    // Default ON so a DB predating this key (or a fresh one before seedDefaults)
    // does not silently disable every sense.
    return db_.getBool(keys::MasterEnabled, true);
}

bool Config::getBool(const char* key, bool respectMaster) const {
    const bool raw = db_.getBool(key, false);
    if (respectMaster && raw && isMasterGated(key) && !masterEnabled())
        return false;   // master kill-switch overrides an individually-on sense
    return raw;
}

int Config::getInt(const char* key, int def) const {
    std::string v = db_.getSetting(key, std::to_string(def));
    try { return std::stoi(v); } catch (...) { return def; }
}

std::string Config::getStr(const char* key, const std::string& def) const {
    return db_.getSetting(key, def);
}

void Config::set(const char* key, const std::string& value) { db_.setSetting(key, value); }

} // namespace polymath
