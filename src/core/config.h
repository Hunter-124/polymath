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
    // Overhaul2 C3: desktop screen capture / describe tools (default ON).
    inline constexpr const char* ScreenCapture         = "privacy.screen_capture";
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
    // E4: minutes of AI present/fullscreen/on-top takeover before auto-revert.
    inline constexpr const char* UiPresentTimeoutMin   = "ui.present_timeout_min";
    inline constexpr const char* AudioInputDevice      = "audio.input_device";
    inline constexpr const char* AudioOutputDevice     = "audio.output_device";
    inline constexpr const char* AudioAsrIdleUnloadS   = "audio.asr_idle_unload_s";
    inline constexpr const char* AppStartMinimized     = "app.start_minimized";
    inline constexpr const char* AppLaunchOnLogin      = "app.launch_on_login";
    inline constexpr const char* AgentsAllowedDirs     = "agents.allowed_dirs";
    inline constexpr const char* AgentsMaxConcurrent   = "agents.max_concurrent";
    inline constexpr const char* AgentsSpeakNeedsInput = "agents.speak_needs_input";
    // A2: minutes a goal may sit parked waiting_agent on an external session
    // before the harness gives up hanging and runs a reflect round instead.
    inline constexpr const char* AgentsJoinTimeoutMin  = "agents.join_timeout_min";
    inline constexpr const char* LlmKvQuant            = "llm.kv_quant";          // q8_0 | f16
    inline constexpr const char* AgentGoalTimeoutMin   = "agent.goal_timeout_min";
    inline constexpr const char* AgentSpeakResults     = "agent.speak_results";

    // --- Overhaul2 D4: TTS v2 (engine/voice/speed/volume) ---------------------
    inline constexpr const char* TtsEngine             = "tts.engine";  // auto|kokoro|piper
    inline constexpr const char* TtsVoice              = "tts.voice";   // default af_heart
    inline constexpr const char* TtsSpeed              = "tts.speed";   // 0.8-1.3, default 1.0
    inline constexpr const char* TtsVolume             = "tts.volume";  // default 1.0

    // --- Overhaul2 A4: SafetyPolicy (risk-gate enforcement) -------------------
    // Enforcement mode: strict (more confirms) | standard | trusted (fewer).
    inline constexpr const char* SafetyMode            = "safety.mode";
    // Base auto-allow ceiling (read|write_local|external|spend|destructive). The
    // effective ceiling is this shifted by the mode (see safety_policy.cpp).
    inline constexpr const char* SafetyAutoconfirmRiskMax = "safety.autoconfirm_risk_max";
    // ';'-separated allowed filesystem roots. Special tokens Documents/Desktop/
    // Downloads/@data resolve at runtime; everything else is a literal path. The
    // agents.allowed_dirs are unioned in automatically.
    inline constexpr const char* SafetyFsAllowedRoots  = "safety.fs_allowed_roots";
    // ';'-separated shell-style globs that are always denied (even inside a root).
    inline constexpr const char* SafetyFsDeniedGlobs   = "safety.fs_denied_globs";
    // ';'-separated regexes; a command arg matching any is denied outright.
    inline constexpr const char* SafetyCmdDenylist     = "safety.cmd_denylist";
    // Max size (KB) of a file-write payload before it is denied.
    inline constexpr const char* SafetyMaxFileWriteKb  = "safety.max_file_write_kb";
    // Record every gated invocation (decision + reason) to the ActivityLog.
    inline constexpr const char* SafetyAudit           = "safety.audit";
    // C1: ';'-separated tool names the user permanently auto-allowed via the
    // confirm dialog's "Always allow this tool". Deny (path/cmd/write-cap) still
    // wins — overrides only skip the risk/mode Confirm gate.
    inline constexpr const char* SafetyToolOverrides   = "safety.tool_overrides";

    // Wave Z: browser_drive host allowlist (';'-separated hosts; empty = open).
    inline constexpr const char* BrowserAllowlist      = "browser.allowlist";
    // When true (default), file:// navigations are denied for browser_drive.
    inline constexpr const char* BrowserBlockFile      = "browser.block_file";
    // YouTube SponsorBlock segment skip (YtClean).
    inline constexpr const char* VideoSponsorBlock     = "video.sponsorblock";
    // Local .ics paths for advisor calendar_read (';'-separated).
    inline constexpr const char* AdvisorCalendarPaths  = "advisor.calendar_paths";
    // Local drop folder for inbox_notes (.eml/.txt).
    inline constexpr const char* AdvisorInboxDir       = "advisor.inbox_dir";
    // Opt-in update check URL (JSON: version, url, sha256).
    inline constexpr const char* UpdatesEnabled        = "updates.enabled";
    inline constexpr const char* UpdatesCheckUrl       = "updates.check_url";
    // Wave Z: face-matched household user (-1 or empty = shared).
    inline constexpr const char* ActiveUserId          = "identity.active_user_id";
    // Optional IMAP (app password) for email_fetch.
    inline constexpr const char* AdvisorImapHost       = "advisor.imap_host";
    inline constexpr const char* AdvisorImapUser       = "advisor.imap_user";
    inline constexpr const char* AdvisorImapPass       = "advisor.imap_pass";
    inline constexpr const char* AdvisorImapPort       = "advisor.imap_port";
}

class Config {
public:
    explicit Config(Database& db) : db_(db) {}
    void seedDefaults();   // inserts any missing keys with product defaults

    // Reads a boolean setting. For the per-feature privacy sense toggles
    // (mic / ambient / face / cameras / screen_capture) the result is AND-ed
    // with the master kill-switch: if privacy.master_enabled is OFF this returns
    // false even when the feature's own value is "1". Pass respectMaster=false
    // to read the raw stored value (e.g. so the UI can show the toggle's own state).
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
