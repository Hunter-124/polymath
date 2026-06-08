#pragma once
//
// Config — process-wide settings facade.  Defaults are defined here; persisted
// overrides live in the `settings` table (so the Privacy/Settings UI can change
// them at runtime).  Privacy toggles default ON per the product decision.
//
#include <string>

namespace polymath {

class Database;

namespace keys {
    // Privacy (all default ON, individually toggleable).
    inline constexpr const char* MicEnabled            = "privacy.mic_enabled";
    inline constexpr const char* AmbientTranscription  = "privacy.ambient_transcription";
    inline constexpr const char* FaceRecognition       = "privacy.face_recognition";
    inline constexpr const char* CamerasEnabled        = "privacy.cameras_enabled";
    inline constexpr const char* EncryptAtRest         = "privacy.encrypt_at_rest";
    // Retention (days; 0 = keep forever).
    inline constexpr const char* RetainAmbientDays     = "retention.ambient_days";
    inline constexpr const char* RetainEventsDays      = "retention.events_days";
    // Behaviour.
    inline constexpr const char* QuietHoursStart       = "behavior.quiet_start"; // "22:00"
    inline constexpr const char* QuietHoursEnd         = "behavior.quiet_end";   // "07:00"
    inline constexpr const char* WakeWord              = "audio.wake_word";      // "hey_jarvis"
    inline constexpr const char* SearchBackend         = "web.search_backend";   // searxng|brave|ddg
    inline constexpr const char* SearchApiKey          = "web.search_api_key";
}

class Config {
public:
    explicit Config(Database& db) : db_(db) {}
    void seedDefaults();   // inserts any missing keys with product defaults

    bool        getBool(const char* key) const;
    int         getInt(const char* key, int def = 0) const;
    std::string getStr(const char* key, const std::string& def = "") const;
    void        set(const char* key, const std::string& value);

private:
    Database& db_;
};

} // namespace polymath
