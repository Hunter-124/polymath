#include "config.h"
#include "database.h"
#include <array>
#include <utility>

namespace polymath {

void Config::seedDefaults() {
    // {key, default}. Privacy defaults ON (product decision: configurable, default-on).
    static const std::array<std::pair<const char*, const char*>, 14> defaults = {{
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
    }};
    for (const auto& [k, v] : defaults)
        if (db_.getSetting(k, "\0__missing__").empty() || db_.getSetting(k, "__missing__") == "__missing__")
            db_.setSetting(k, v);
}

bool Config::getBool(const char* key) const { return db_.getBool(key, false); }

int Config::getInt(const char* key, int def) const {
    std::string v = db_.getSetting(key, std::to_string(def));
    try { return std::stoi(v); } catch (...) { return def; }
}

std::string Config::getStr(const char* key, const std::string& def) const {
    return db_.getSetting(key, def);
}

void Config::set(const char* key, const std::string& value) { db_.setSetting(key, value); }

} // namespace polymath
