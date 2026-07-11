#include "safety_policy.h"
#include "config.h"
#include "database.h"
#include "paths.h"
#include "logging.h"

#include <QStandardPaths>
#include <QString>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace polymath {
namespace core {

// --- small helpers -----------------------------------------------------------
namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> splitList(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { auto t = trim(cur); if (!t.empty()) out.push_back(t); cur.clear(); }
        else cur.push_back(c);
    }
    auto t = trim(cur);
    if (!t.empty()) out.push_back(t);
    return out;
}

// Normalize a filesystem path to a lower-cased, forward-slash, trailing-slash-
// free string suitable for prefix / glob comparison.
std::string normPath(const std::string& p) {
    if (p.empty()) return {};
    std::error_code ec;
    std::filesystem::path fp(p);
    std::filesystem::path canon = std::filesystem::weakly_canonical(fp, ec);
    std::string s = ec ? fp.lexically_normal().string() : canon.string();
    for (char& c : s) { if (c == '\\') c = '/'; c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

bool withinRoot(const std::string& child, const std::string& root) {
    if (root.empty() || child.empty()) return false;
    if (child == root) return true;
    return child.size() > root.size() &&
           child.compare(0, root.size(), root) == 0 &&
           child[root.size()] == '/';
}

// Translate a shell-style glob (lower-cased) into a regex. '*' -> '.*',
// '?' -> '.', every other regex metacharacter is escaped. Matched with
// regex_search so a bare "*/appdata/*" hits any substring.
std::regex compileGlob(const std::string& glob) {
    std::string re;
    re.reserve(glob.size() * 2);
    for (char c : glob) {
        switch (c) {
        case '*': re += ".*"; break;
        case '?': re += '.'; break;
        case '.': case '\\': case '+': case '(': case ')': case '[': case ']':
        case '{': case '}': case '^': case '$': case '|':
            re += '\\'; re += c; break;
        default: re += c;
        }
    }
    return std::regex(re, std::regex::icase | std::regex::optimize);
}

// Argument keys that carry a filesystem path (checked against roots + globs).
bool isPathKey(const std::string& k) {
    static const char* keys[] = {"path", "src", "source", "dst", "dest",
                                 "destination", "file", "filepath", "filename",
                                 "cwd", "dir", "directory", "target"};
    const std::string lk = toLower(k);
    for (const char* c : keys) if (lk == c) return true;
    return false;
}

// Argument keys that carry a shell command line (scanned against the denylist).
bool isCommandKey(const std::string& k) {
    static const char* keys[] = {"command", "cmd", "commandline", "script"};
    const std::string lk = toLower(k);
    for (const char* c : keys) if (lk == c) return true;
    return false;
}

// Collect string values for path- / command-like keys (top level of args).
void collect(const nlohmann::json& args,
             std::vector<std::string>& paths,
             std::vector<std::string>& cmds) {
    if (!args.is_object()) return;
    for (auto it = args.begin(); it != args.end(); ++it) {
        const std::string key = it.key();
        const auto& v = it.value();
        if (isPathKey(key) && v.is_string()) paths.push_back(v.get<std::string>());
        if (isCommandKey(key)) {
            if (v.is_string()) cmds.push_back(v.get<std::string>());
            else if (v.is_array()) {          // e.g. args:[...]
                std::string joined;
                for (const auto& e : v) if (e.is_string()) { joined += e.get<std::string>(); joined += ' '; }
                if (!joined.empty()) cmds.push_back(joined);
            }
        }
    }
    // "args" of type array is a common command-vector shape too.
    if (args.contains("args") && args["args"].is_array()) {
        std::string joined;
        for (const auto& e : args["args"]) if (e.is_string()) { joined += e.get<std::string>(); joined += ' '; }
        if (!joined.empty()) cmds.push_back(joined);
    }
}

long long payloadBytes(const nlohmann::json& args) {
    long long n = 0;
    for (const char* k : {"content", "text", "body", "data"}) {
        if (args.contains(k) && args[k].is_string())
            n = std::max<long long>(n, static_cast<long long>(args[k].get<std::string>().size()));
    }
    return n;
}

} // namespace

RiskLevel riskLevelFromString(const std::string& s) {
    const std::string l = toLower(trim(s));
    if (l == "read")         return RiskLevel::Read;
    if (l == "write_local" || l == "writelocal" || l == "write") return RiskLevel::WriteLocal;
    if (l == "external")     return RiskLevel::External;
    if (l == "spend")        return RiskLevel::Spend;
    if (l == "destructive")  return RiskLevel::Destructive;
    return RiskLevel::WriteLocal;
}

const char* riskLevelName(RiskLevel r) {
    switch (r) {
    case RiskLevel::Read:        return "read";
    case RiskLevel::WriteLocal:  return "write_local";
    case RiskLevel::External:    return "external";
    case RiskLevel::Spend:       return "spend";
    case RiskLevel::Destructive: return "destructive";
    }
    return "read";
}

// --- config-backed compiled cache -------------------------------------------

void SafetyPolicy::refresh() const {
    Config cfg(db_);
    const std::string mode     = cfg.getStr(keys::SafetyMode, "standard");
    const std::string autoconf = cfg.getStr(keys::SafetyAutoconfirmRiskMax, "write_local");
    const std::string roots    = cfg.getStr(keys::SafetyFsAllowedRoots,
                                            "Documents;Desktop;Downloads;@data");
    const std::string agentDirs= cfg.getStr(keys::AgentsAllowedDirs, "");
    const std::string globs    = cfg.getStr(keys::SafetyFsDeniedGlobs, "");
    const std::string cmds     = cfg.getStr(keys::SafetyCmdDenylist, "");
    const std::string maxkb    = cfg.getStr(keys::SafetyMaxFileWriteKb, "2048");

    if (built_ &&
        cache_.raw_mode == mode && cache_.raw_autoconfirm == autoconf &&
        cache_.raw_roots == roots && cache_.raw_agent_dirs == agentDirs &&
        cache_.raw_globs == globs && cache_.raw_cmds == cmds &&
        cache_.raw_maxkb == maxkb) {
        return;   // nothing changed
    }

    Compiled c;
    c.raw_mode = mode; c.raw_autoconfirm = autoconf; c.raw_roots = roots;
    c.raw_agent_dirs = agentDirs; c.raw_globs = globs; c.raw_cmds = cmds;
    c.raw_maxkb = maxkb;

    c.mode = toLower(trim(mode.empty() ? "standard" : mode));
    c.autoconfirm_max = riskLevelFromString(autoconf);
    try { c.max_write_bytes = std::max<long long>(0, std::stoll(trim(maxkb))) * 1024; }
    catch (...) { c.max_write_bytes = 2048LL * 1024; }

    // Allowed roots: resolve special tokens, union with agents.allowed_dirs so a
    // directory the user already authorized for external agent sessions is also
    // writable by the fs_* tools (keeps agent_spawn's cwd from being denied).
    for (const std::string& tok : splitList(roots, ';')) {
        const std::string l = toLower(tok);
        std::string resolved;
        if (l == "documents")
            resolved = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toStdString();
        else if (l == "desktop")
            resolved = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation).toStdString();
        else if (l == "downloads")
            resolved = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation).toStdString();
        else if (l == "@data" || l == "data")
            resolved = Paths::instance().root().string();
        else
            resolved = tok;   // literal path
        const std::string n = normPath(resolved);
        if (!n.empty()) c.allowed_roots.push_back(n);
    }
    for (const std::string& d : splitList(agentDirs, ';')) {
        const std::string n = normPath(d);
        if (!n.empty()) c.allowed_roots.push_back(n);
    }
    std::sort(c.allowed_roots.begin(), c.allowed_roots.end());
    c.allowed_roots.erase(std::unique(c.allowed_roots.begin(), c.allowed_roots.end()),
                          c.allowed_roots.end());

    for (const std::string& g : splitList(globs, ';')) {
        try { c.denied_globs.push_back(compileGlob(toLower(g))); }
        catch (const std::exception& e) { PM_WARN("SafetyPolicy: bad denied glob '{}': {}", g, e.what()); }
    }
    for (const std::string& r : splitList(cmds, ';')) {
        try { c.cmd_denylist.emplace_back(r, std::regex::icase | std::regex::optimize); }
        catch (const std::exception& e) { PM_WARN("SafetyPolicy: bad cmd regex '{}': {}", r, e.what()); }
    }

    cache_ = std::move(c);
    built_ = true;
}

bool SafetyPolicy::auditEnabled() const {
    // Defaults ON: treat a missing key as enabled (the app seeds "1").
    return Config(db_).getStr(keys::SafetyAudit, "1") != "0";
}

// --- the decision ------------------------------------------------------------

Ruling SafetyPolicy::check(const std::string& tool, RiskLevel risk,
                           const nlohmann::json& args) const {
    std::lock_guard<std::mutex> lk(mtx_);
    refresh();
    const Compiled& c = cache_;

    std::vector<std::string> paths, cmds;
    collect(args, paths, cmds);

    // 1) Path arguments: must sit inside an allowed root AND clear the denied
    //    globs. Deny wins over everything.
    for (const std::string& raw : paths) {
        const std::string np = normPath(raw);
        if (np.empty()) continue;
        for (const std::regex& g : c.denied_globs) {
            if (std::regex_search(np, g))
                return {Decision::Deny, "path is protected by a safety rule: " + raw};
        }
        bool inside = false;
        for (const std::string& root : c.allowed_roots)
            if (withinRoot(np, root)) { inside = true; break; }
        if (!c.allowed_roots.empty() && !inside)
            return {Decision::Deny, "path is outside the allowed roots: " + raw};
    }

    // 2) Command arguments: block anything matching the denylist regexes.
    for (const std::string& cmd : cmds) {
        for (const std::regex& re : c.cmd_denylist) {
            if (std::regex_search(cmd, re)) {
                std::string shown = cmd.size() > 80 ? cmd.substr(0, 80) + "…" : cmd;
                return {Decision::Deny, "command blocked by safety denylist: " + shown};
            }
        }
    }

    // 3) Oversized write payloads.
    if (!paths.empty() && c.max_write_bytes > 0) {
        const long long n = payloadBytes(args);
        if (n > c.max_write_bytes)
            return {Decision::Deny,
                    "write payload (" + std::to_string(n / 1024) + " KB) exceeds the "
                    + std::to_string(c.max_write_bytes / 1024) + " KB limit"};
    }

    // 3b) schedule_task standing rules (every/rrule) always need Confirm even
    // though the tool's static registration is WriteLocal (D1 design note).
    // One-shot `when.at` stays under the normal risk/mode gate.
    if (tool == "schedule_task" && args.is_object() && args.contains("when")
        && args["when"].is_object()) {
        const auto& when = args["when"];
        const bool recurring =
            (when.contains("every_s") && !when["every_s"].is_null()) ||
            (when.contains("every")   && !when["every"].is_null()) ||
            (when.contains("rrule") && when["rrule"].is_string()
             && !when["rrule"].get<std::string>().empty());
        if (recurring)
            return {Decision::Confirm,
                    "standing schedule (recurring) needs your approval"};
    }

    // 4) Risk / mode gate. Destructive is NEVER auto-confirmed in any mode.
    if (risk == RiskLevel::Destructive)
        return {Decision::Confirm, "destructive action needs your approval"};

    // Effective auto-allow ceiling = configured base, shifted by the mode:
    //   strict   = base            (default WriteLocal → External+ confirm)
    //   standard = base + 1        (default External   → Spend/Destructive confirm)
    //   trusted  = base + 2        (default Spend      → only Destructive confirm)
    // Clamped to <= Spend so Destructive can never become auto (also guarded above).
    int base = static_cast<int>(c.autoconfirm_max);
    int shift = (c.mode == "strict") ? 0 : (c.mode == "trusted") ? 2 : 1;
    int eff = std::min(base + shift, static_cast<int>(RiskLevel::Spend));

    if (static_cast<int>(risk) <= eff)
        return {Decision::Allow, "within auto-allow policy"};

    return {Decision::Confirm,
            std::string(riskLevelName(risk)) + " action needs your approval (" + c.mode + " mode)"};
}

// --- presentation helpers ----------------------------------------------------

std::string SafetyPolicy::argsPreview(const nlohmann::json& args) {
    std::string s = args.is_null() ? "{}" : args.dump();
    if (s.size() > 400) s = s.substr(0, 400) + "…";
    return s;
}

std::string SafetyPolicy::describe(const std::string& tool, const nlohmann::json& args) {
    // A short, human-facing action line. Prefer the most telling arg.
    auto firstStr = [&](std::initializer_list<const char*> ks) -> std::string {
        for (const char* k : ks)
            if (args.is_object() && args.contains(k) && args[k].is_string())
                return args[k].get<std::string>();
        return {};
    };
    const std::string path = firstStr({"path", "dst", "file", "target", "src"});
    const std::string cmd  = firstStr({"command", "cmd", "script"});
    if (!cmd.empty())
        return tool + ": run \"" + (cmd.size() > 80 ? cmd.substr(0, 80) + "…" : cmd) + "\"";
    if (!path.empty())
        return tool + ": " + path;
    return tool;
}

} // namespace core
} // namespace polymath
