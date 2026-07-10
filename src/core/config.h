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
    // Master privacy kill-switch. When OFF, every per-feature sense toggle below
    // reads as OFF regardless of its own value (enforced centrally in
    // Config::getBool), so flipping this one key stops all capture at once.
    inline constexpr const char* MasterEnabled         = "privacy.master_enabled";
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
    // First-run flow acknowledged ("1" once the user has been through setup).
    // Absent/"0" => still first run (drives the cold-start + opt-in banners).
    inline constexpr const char* FirstRunDone          = "app.first_run_done";

    // --- Overhaul (A2): UI / audio / agents / inference knobs -----------------
    inline constexpr const char* UiAccent              = "ui.accent";
    inline constexpr const char* UiEffects             = "ui.effects";
    inline constexpr const char* UiEffectsIntensity    = "ui.effects_intensity";
    inline constexpr const char* UiFontScale           = "ui.font_scale";
    inline constexpr const char* UiReduceMotion        = "ui.reduce_motion";
    inline constexpr const char* AudioInputDevice      = "audio.input_device";
    inline constexpr const char* AudioOutputDevice     = "audio.output_device";
    inline constexpr const char* AudioAsrIdleUnloadS   = "audio.asr_idle_unload_s";
    inline constexpr const char* AppStartMinimized     = "app.start_minimized";
    inline constexpr const char* AppLaunchOnLogin      = "app.launch_on_login";
    inline constexpr const char* AgentsAllowedDirs     = "agents.allowed_dirs";
    inline constexpr const char* AgentsMaxConcurrent   = "agents.max_concurrent";
    inline constexpr const char* AgentsSpeakNeedsInput = "agents.speak_needs_input";
    inline constexpr const char* LlmKvQuant            = "llm.kv_quant";          // q8_0 | f16
    inline constexpr const char* AgentGoalTimeoutMin   = "agent.goal_timeout_min";
    inline constexpr const char* AgentSpeakResults     = "agent.speak_results";
}

class Config {
public:
    explicit Config(Database& db) : db_(db) {}
    void seedDefaults();   // inserts any missing keys with product defaults

    // Reads a boolean setting. For the per-feature privacy sense toggles
    // (mic / ambient / face / cameras) the result is AND-ed with the master
    // kill-switch: if privacy.master_enabled is OFF this returns false even when
    // the feature's own value is "1". Pass respectMaster=false to read the raw
    // stored value (e.g. so the UI can show the toggle's own state).
    bool        getBool(const char* key, bool respectMaster = true) const;
    int         getInt(const char* key, int def = 0) const;
    std::string getStr(const char* key, const std::string& def = "") const;
    void        set(const char* key, const std::string& value);

    // True when the master privacy kill-switch is on (defaults ON).
    bool        masterEnabled() const;
    // True if `key` is a per-feature sense toggle gated by the master switch.
    static bool isMasterGated(const char* key);

private:
    Database& db_;
};

} // namespace polymath
