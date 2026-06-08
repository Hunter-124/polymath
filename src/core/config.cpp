#include "config.h"
#include "database.h"
#include <array>
#include <utility>

namespace polymath {

void Config::seedDefaults() {
    // {key, default}. Privacy defaults ON (product decision: configurable, default-on).
    // Size is deduced from the initializers — do NOT hard-code a count (a
    // mismatch leaves {nullptr,nullptr} pairs that crash on use).
    static const std::pair<const char*, const char*> defaults[] = {
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
    };
    // INSERT OR IGNORE seeds only missing keys (never clobbers user changes),
    // since settings.key is the PRIMARY KEY.
    for (const auto& [k, v] : defaults)
        db_.exec("INSERT OR IGNORE INTO settings(key,value) VALUES(?1,?2)",
                 {std::string(k), std::string(v)});
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
