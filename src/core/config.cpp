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
        {keys::ScreenCapture,        "1"},
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
        // E4: AI window-takeover auto-revert (Main.qml presentTimeoutTimer).
        {keys::UiPresentTimeoutMin,  "30"},
        {keys::AudioInputDevice,     ""},
        {keys::AudioOutputDevice,    ""},
        {keys::AudioAsrIdleUnloadS,  "90"},
        {keys::AppStartMinimized,    "0"},
        {keys::AppLaunchOnLogin,     "0"},
        {keys::AgentsAllowedDirs,    ""},
        {keys::AgentsMaxConcurrent,  "2"},
        {keys::AgentsSpeakNeedsInput,"1"},
        {keys::AgentsJoinTimeoutMin, "120"},
        {keys::LlmKvQuant,           "q8_0"},
        {keys::AgentGoalTimeoutMin,  "30"},
        {keys::AgentSpeakResults,    "1"},
        // Overhaul2 D4 defaults (docs/overhaul2/01_DAG.md, node D4).
        {keys::TtsEngine,            "auto"},
        {keys::TtsVoice,             "af_heart"},
        {keys::TtsSpeed,             "1.0"},
        {keys::TtsVolume,            "1.0"},
        // Overhaul2 A4 defaults — SafetyPolicy (docs/overhaul2/01_DAG.md, node A4).
        // Default base ceiling is write_local; standard mode shifts it +1 so
        // read/write_local/external auto-run and spend/destructive confirm
        // (matches the historical toolRiskRequiresConfirmation baseline + B1's
        // "External auto-allowed by default policy"). See safety_policy.cpp.
        {keys::SafetyMode,               "standard"},
        {keys::SafetyAutoconfirmRiskMax, "write_local"},
        {keys::SafetyFsAllowedRoots,     "Documents;Desktop;Downloads;@data"},
        // Denied even inside an allowed root (deny wins). Location protection for
        // AppData/Windows/ProgramFiles is primarily the allowed-roots whitelist's
        // job (those are not default roots); AppData is intentionally NOT globbed
        // here because the installed data dir lives under %LOCALAPPDATA%/Polymath
        // and IS an allowed root — a blanket AppData deny would block our own
        // captures/documents. These globs catch dangerous paths that can appear
        // INSIDE an allowed root: git internals + the Polymath DB files.
        {keys::SafetyFsDeniedGlobs,
             "*/.git/*;C:/Windows/*;C:/Program Files*;C:/Program Files (x86)*;"
             "*/polymath.db;*/polymath.db-wal;*/polymath.db-shm"},
        {keys::SafetyCmdDenylist,
             // NOTE: the list separator is ';' — no regex below may contain a
             // literal ';' (it would be split mid-pattern).
             "(?:^|[\\s&|])format(?:\\s|$);"         // format C:
             "\\bdel\\b[^\\n]*\\s/[sqf];"            // del /s /q /f
             "\\brd\\b[^\\n]*\\s/s;\\brmdir\\b[^\\n]*\\s/s;"  // rd /s
             "\\brm\\b[^\\n]*\\s-[a-z]*[rf];"        // rm -rf / rm -fr
             "Remove-Item[^\\n]*-Recurse;"           // PowerShell recursive delete
             "\\breg\\b\\s+(?:add|delete);"          // registry writes
             "\\bshutdown\\b;\\brestart-computer\\b;"
             "\\bdiskpart\\b;\\bmkfs\\b;\\bfdisk\\b;"
             "\\bcipher\\b[^\\n]*/w;"                 // cipher /w (wipe)
             "\\bformat-volume\\b;\\bclear-disk\\b"},
        {keys::SafetyMaxFileWriteKb,     "2048"},
        {keys::SafetyAudit,              "1"},
        // C1: per-tool "always allow" list (written by ConfirmDialog Always allow).
        {keys::SafetyToolOverrides,      ""},
        // Wave Z
        {keys::BrowserAllowlist,         ""},
        {keys::BrowserBlockFile,         "1"},
        {keys::VideoSponsorBlock,        "1"},
        {keys::AdvisorCalendarPaths,     ""},
        {keys::AdvisorInboxDir,          ""},
        {keys::UpdatesEnabled,           "0"},
        {keys::UpdatesCheckUrl,          ""},
        {keys::ActiveUserId,             "-1"},
        {keys::AdvisorImapHost,          ""},
        {keys::AdvisorImapUser,          ""},
        {keys::AdvisorImapPass,          ""},
        {keys::AdvisorImapPort,          "993"},
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
        || std::strcmp(key, keys::CamerasEnabled) == 0
        || std::strcmp(key, keys::ScreenCapture) == 0;
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
