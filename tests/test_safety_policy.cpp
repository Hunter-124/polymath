// SafetyPolicy risk-gate unit tests (overhaul2 A4).
//
// Pure decision logic over a temp DB + Config: the path allow/deny matrix
// (inside/outside roots, denied globs), the command denylist (rm -rf / format),
// mode escalation (strict/standard/trusted), the write-size cap, and the
// invariant that Destructive is NEVER auto-confirmed. No model, no agent stack.

#include "database.h"
#include "config.h"
#include "safety_policy.h"

#include <QCoreApplication>

#undef NDEBUG   // keep assert() live in Release
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace polymath;
using core::Decision;
using core::RiskLevel;
using core::SafetyPolicy;

namespace {

const char* dn(Decision d) { return core::decisionName(d); }

// Normalize like the policy does so test paths compare exactly.
std::string join(const std::filesystem::path& root, const std::string& rel) {
    return (root / rel).string();
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const auto root = std::filesystem::temp_directory_path() / "pm_safety_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "repo" / ".git");

    const auto dbPath = root / "safety.db";
    std::filesystem::remove(dbPath, ec);
    Database db;
    assert(db.open(dbPath.string()));
    Config cfg(db);
    cfg.seedDefaults();

    // Deterministic rules: one absolute allowed root, controlled globs. Keep the
    // default cmd denylist (rm -rf / format / ...).
    cfg.set(keys::SafetyFsAllowedRoots, root.string());
    cfg.set(keys::SafetyFsDeniedGlobs, "*/.git/*;*secret*");
    cfg.set(keys::AgentsAllowedDirs, "");

    SafetyPolicy policy(db);

    std::puts("test_safety_policy: A4 risk-gate");

    // --- 1) Path allow/deny matrix -----------------------------------------
    cfg.set(keys::SafetyMode, "standard");
    {
        // Inside an allowed root, no glob → Allow (path check passes; Read auto).
        nlohmann::json a = {{"path", join(root, "docs/note.txt")}};
        auto r = policy.check("fs_read", RiskLevel::Read, a);
        assert(r.decision == Decision::Allow && "inside-root path should Allow");

        // Outside every allowed root → Deny.
        nlohmann::json b = {{"path", "C:/Windows/system32/evil.dll"}};
        auto r2 = policy.check("fs_read", RiskLevel::Read, b);
        assert(r2.decision == Decision::Deny && "outside-root path must Deny");

        // Inside a root but matching a denied glob (.git) → Deny wins.
        nlohmann::json c = {{"path", join(root, "repo/.git/config")}};
        auto r3 = policy.check("fs_write", RiskLevel::WriteLocal, c);
        assert(r3.decision == Decision::Deny && "denied glob must Deny even inside a root");

        // Denied substring glob (*secret*).
        nlohmann::json d = {{"dst", join(root, "my_secret_keys.txt")}};
        auto r4 = policy.check("fs_write", RiskLevel::WriteLocal, d);
        assert(r4.decision == Decision::Deny && "secret glob must Deny");

        std::puts("  [ok] path matrix: inside=allow, outside=deny, denied-glob=deny");
    }

    // --- 2) Command denylist ------------------------------------------------
    {
        nlohmann::json rmrf = {{"command", "rm -rf /home/user"}};
        assert(policy.check("run_command", RiskLevel::External, rmrf).decision == Decision::Deny
               && "rm -rf must be denied");

        nlohmann::json fmt = {{"command", "format C: /q"}};
        assert(policy.check("run_command", RiskLevel::External, fmt).decision == Decision::Deny
               && "format must be denied");

        nlohmann::json rmdir = {{"command", "Remove-Item -Recurse -Force C:/data"}};
        assert(policy.check("run_command", RiskLevel::External, rmdir).decision == Decision::Deny
               && "Remove-Item -Recurse must be denied");

        // A benign command (no path arg) clears the denylist; Read auto-allows.
        nlohmann::json ok = {{"command", "echo hello world"}};
        assert(policy.check("run_command", RiskLevel::Read, ok).decision == Decision::Allow
               && "benign command should pass the denylist");
        std::puts("  [ok] cmd denylist: rm -rf / format / Remove-Item -Recurse blocked");
    }

    // --- 3) Mode escalation (risk-only, no path/cmd args) -------------------
    const nlohmann::json none = nlohmann::json::object();

    cfg.set(keys::SafetyMode, "strict");
    assert(policy.check("t", RiskLevel::WriteLocal, none).decision == Decision::Allow);
    assert(policy.check("t", RiskLevel::External,  none).decision == Decision::Confirm
           && "strict: external needs confirm");

    cfg.set(keys::SafetyMode, "standard");
    assert(policy.check("t", RiskLevel::Read,      none).decision == Decision::Allow);
    assert(policy.check("t", RiskLevel::External,  none).decision == Decision::Allow
           && "standard: external auto-allowed (B1 contract)");
    assert(policy.check("t", RiskLevel::Spend,     none).decision == Decision::Confirm
           && "standard: spend needs confirm");

    cfg.set(keys::SafetyMode, "trusted");
    assert(policy.check("t", RiskLevel::Spend,       none).decision == Decision::Allow
           && "trusted: spend auto-allowed");
    assert(policy.check("t", RiskLevel::Destructive, none).decision == Decision::Confirm
           && "trusted: destructive still needs confirm");
    std::puts("  [ok] mode escalation: strict < standard < trusted");

    // --- 4) Destructive is NEVER auto-confirmed, in ANY mode ---------------
    for (const char* mode : {"strict", "standard", "trusted"}) {
        cfg.set(keys::SafetyMode, mode);
        const auto r = policy.check("fs_delete", RiskLevel::Destructive, none);
        assert(r.decision == Decision::Confirm &&
               "destructive must always Confirm (never Allow)");
    }
    // Even if a misconfig pushes the ceiling to 'destructive', it stays gated.
    cfg.set(keys::SafetyMode, "trusted");
    cfg.set(keys::SafetyAutoconfirmRiskMax, "destructive");
    assert(policy.check("fs_delete", RiskLevel::Destructive, none).decision == Decision::Confirm
           && "destructive stays gated even at max ceiling");
    cfg.set(keys::SafetyAutoconfirmRiskMax, "write_local");
    std::puts("  [ok] destructive never auto-confirmed");

    // --- 5) Write-size cap --------------------------------------------------
    cfg.set(keys::SafetyMode, "standard");
    cfg.set(keys::SafetyMaxFileWriteKb, "1");   // 1 KB
    {
        nlohmann::json big = {{"path", join(root, "big.txt")},
                              {"content", std::string(4096, 'x')}};
        assert(policy.check("fs_write", RiskLevel::WriteLocal, big).decision == Decision::Deny
               && "oversized write must be denied");
        nlohmann::json small = {{"path", join(root, "small.txt")},
                                {"content", std::string(10, 'x')}};
        assert(policy.check("fs_write", RiskLevel::WriteLocal, small).decision == Decision::Allow
               && "small write within the cap should Allow");
    }
    std::puts("  [ok] file-write size cap enforced");

    // --- 6) Deny wins over Confirm -----------------------------------------
    // A Destructive tool whose path is outside roots → Deny (not Confirm).
    cfg.set(keys::SafetyMode, "standard");
    {
        nlohmann::json a = {{"path", "C:/Windows/foo"}};
        const auto r = policy.check("fs_delete", RiskLevel::Destructive, a);
        assert(r.decision == Decision::Deny && "deny must win over confirm");
    }
    std::puts("  [ok] deny wins over confirm");

    // --- 7) schedule_task standing rules escalate to Confirm (D1) ----------
    cfg.set(keys::SafetyMode, "standard");
    {
        // One-shot `at` stays under normal WriteLocal auto-allow.
        nlohmann::json once = {
            {"title", "remind me"},
            {"when", {{"at", "2026-07-11T10:00:00"}}},
            {"task", {{"kind", "prompt"}, {"text", "hi"}}},
        };
        assert(policy.check("schedule_task", RiskLevel::WriteLocal, once).decision
                   == Decision::Allow
               && "one-shot schedule_task should auto-allow under WriteLocal");

        nlohmann::json every = {
            {"title", "daily brief"},
            {"when", {{"every_s", 86400}}},
            {"task", {{"kind", "skill"}, {"name", "morning_brief"}}},
        };
        assert(policy.check("schedule_task", RiskLevel::WriteLocal, every).decision
                   == Decision::Confirm
               && "recurring every_s must Confirm");

        nlohmann::json rrule = {
            {"title", "weekly"},
            {"when", {{"rrule", "FREQ=WEEKLY;BYDAY=MO"}}},
            {"task", {{"kind", "prompt"}, {"text", "check-in"}}},
        };
        assert(policy.check("schedule_task", RiskLevel::WriteLocal, rrule).decision
                   == Decision::Confirm
               && "rrule standing rule must Confirm");
    }
    std::puts("  [ok] schedule_task recurring → Confirm, one-shot → Allow");

    db.close();
    std::filesystem::remove_all(root, ec);
    std::printf("test_safety_policy: ALL CHECKS PASSED (last sample decision=%s)\n",
                dn(policy.check("t", RiskLevel::Read, none).decision));
    return 0;
}
