#include "personality_manager.h"
#include "bundle_seed.h"
#include "database.h"
#include "paths.h"
#include "logging.h"
#include "event_bus.h"

#include <QFileSystemWatcher>
#include <QString>
#include <QStringList>
#include <QDir>

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

// PersonalityManager — loads modular persona bundles from personalities/<name>/
// persona.json and keeps the `personalities` table in sync so the AgentRuntime
// can read the active persona (system prompt / voice / preferred model / wake
// phrase) straight from the DB.
//
// Lifecycle: constructed by AppController and start()ed on the UI thread (it is
// lightweight — small-JSON parsing plus a handful of indexed upserts).  A
// QFileSystemWatcher on personalities/ triggers a debounced rescan so dropping
// in or editing a bundle hot-reloads without a restart.  All public method and
// signal signatures are frozen; everything added here is private.

namespace polymath {

namespace fs = std::filesystem;

namespace {

// Debounce window after a filesystem change before we rescan.  Editors often
// write a bundle as several rapid events (truncate, write, rename); coalescing
// avoids parsing a half-written persona.json.
constexpr int kRescanDebounceMs = 400;

// The fallback persona used when the personalities/ dir yields nothing usable,
// so active() always has something to return.
Personality defaultPersona() {
    Personality p;
    p.name          = "Assistant";
    p.system_prompt = "You are a helpful local home assistant.";
    p.preferred_model = "fast";
    return p;
}

// ---------------------------------------------------------------------------
//  write-API helpers (overhaul2 E2)
// ---------------------------------------------------------------------------

// Bundle folder names are a single path segment, not a path: reject traversal
// outright and replace filesystem-hostile characters (Windows-illegal plus a
// couple of POSIX-awkward ones) so a persona display name like "Weird: Name?"
// still yields a legal, unsurprising directory.
std::string sanitizeBundleFolder(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return {};
    if (name.find("..") != std::string::npos) return {};
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
            out.push_back('_');
        else
            out.push_back(static_cast<char>(c));
    }
    // Trailing dots/spaces are invalid on Windows and get silently stripped by
    // the OS anyway — strip them ourselves so exists()-checks stay accurate.
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    return out;
}

// tmp-file-then-rename write so a crash or concurrent QFileSystemWatcher scan
// never observes a half-written persona.json.
bool writeJsonAtomic(const fs::path& file, const nlohmann::json& j) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const auto tmp = file.parent_path() / (file.filename().string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
        if (!out) return false;
        const std::string body = j.dump(2);
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!out) return false;
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        // Cross-device or "target exists" on some filesystems — fall back to
        // remove-then-rename rather than leaving the .tmp orphaned.
        std::error_code rmec;
        fs::remove(file, rmec);
        ec.clear();
        fs::rename(tmp, file, ec);
        if (ec) { fs::remove(tmp, rmec); return false; }
    }
    return true;
}

} // namespace

PersonalityManager::~PersonalityManager() = default;  // QFileSystemWatcher complete here

PersonalityManager::PersonalityManager(Database& db, QObject* parent)
    : QObject(parent), db_(db) {
    // Single-shot debounce: every filesystem event restarts the countdown.
    rescan_timer_.setSingleShot(true);
    rescan_timer_.setInterval(kRescanDebounceMs);
    connect(&rescan_timer_, &QTimer::timeout, this, [this] { scanBundles(); });
}

void PersonalityManager::start() {
    const auto dir = Paths::instance().personalities();

    // First run: provision the shipped starter bundles into the (empty) data dir.
    std::error_code ec;
    fs::create_directories(dir, ec);
    seedStarterBundles(dir);

    scanBundles();
    installWatcher();
}

void PersonalityManager::stop() {
    rescan_timer_.stop();
    if (watcher_) {
        const QStringList dirs  = watcher_->directories();
        const QStringList files = watcher_->files();
        if (!dirs.isEmpty())  watcher_->removePaths(dirs);
        if (!files.isEmpty()) watcher_->removePaths(files);
        watcher_.reset();
    }
}

// ---------------------------------------------------------------------------
//  scanning
// ---------------------------------------------------------------------------
void PersonalityManager::scanBundles() {
    if (scanning_) return;                     // re-entrancy guard (watcher churn)
    scanning_ = true;

    // Remember the current selection by name so a rescan doesn't reset which
    // persona is active just because indices shifted.
    const std::string prev_active =
        (active_ >= 0 && active_ < static_cast<int>(personas_.size()))
            ? personas_[active_].name
            : std::string{};

    namespace fsn = std::filesystem;
    std::vector<Personality> loaded;
    const auto dir = Paths::instance().personalities();
    std::error_code ec;
    if (fsn::exists(dir, ec)) {
        for (auto& entry : fsn::directory_iterator(dir, ec)) {
            auto file = entry.path() / "persona.json";
            if (!fsn::exists(file)) continue;
            try {
                std::ifstream in(file);
                auto j = nlohmann::json::parse(in);
                Personality p;
                p.name            = j.value("name", entry.path().filename().string());
                p.system_prompt   = j.value("system_prompt", "");
                p.voice           = j.value("voice", "");
                p.preferred_model = j.value("preferred_model", "fast");
                p.wake_phrase     = j.value("wake_phrase", "");
                p.tools           = j.value("tools", std::vector<std::string>{});
                p.bundle_dir      = entry.path().filename().string();
                if (j.contains("sampling")) {
                    const auto& s = j["sampling"];
                    p.sampling.temperature    = s.value("temperature", p.sampling.temperature);
                    p.sampling.top_p          = s.value("top_p", p.sampling.top_p);
                    p.sampling.top_k          = s.value("top_k", p.sampling.top_k);
                    p.sampling.repeat_penalty = s.value("repeat_penalty", p.sampling.repeat_penalty);
                    p.sampling.max_tokens     = s.value("max_tokens", p.sampling.max_tokens);
                }
                // Resolve an optional avatar living beside persona.json.
                for (const char* fname : {"avatar.png", "avatar.jpg", "avatar.jpeg"}) {
                    const auto avatar = entry.path() / fname;
                    if (fsn::exists(avatar)) { p.avatar_path = avatar.string(); break; }
                }
                loaded.push_back(std::move(p));
            } catch (const std::exception& e) {
                PM_WARN("personality: bad bundle {}: {}", file.string(), e.what());
                EventBus::instance().publishNotice(
                    {"warn", "personality",
                     QStringLiteral("Skipped invalid persona bundle: %1")
                         .arg(QString::fromStdString(entry.path().filename().string()))});
            }
        }
    }

    // Stable, predictable ordering (case-insensitive by display name).
    std::sort(loaded.begin(), loaded.end(),
              [](const Personality& a, const Personality& b) {
                  auto lower = [](std::string s) {
                      std::transform(s.begin(), s.end(), s.begin(),
                                     [](unsigned char c) { return std::tolower(c); });
                      return s;
                  };
                  return lower(a.name) < lower(b.name);
              });

    if (loaded.empty()) {
        PM_WARN("personality: no usable bundles in {}, using built-in default",
                dir.string());
        loaded.push_back(defaultPersona());
    }

    personas_ = std::move(loaded);

    // Reconcile the DB mirror, then settle on the active persona: keep the prior
    // in-memory selection if it still exists, else fall back to what the DB
    // remembered, else the first persona.
    syncDatabase();

    int idx = -1;
    if (!prev_active.empty()) {
        for (size_t i = 0; i < personas_.size(); ++i)
            if (personas_[i].name == prev_active) { idx = static_cast<int>(i); break; }
    }
    if (idx < 0) idx = resolveActiveIndex();
    active_ = idx;

    // Make sure exactly one row is flagged active for the persona we landed on.
    const Personality& cur = personas_[active_];
    persistActive(cur.name);

    PM_INFO("personality: loaded {} personalities (active='{}')",
            personas_.size(), cur.name);

    emit personalitiesChanged();

    // If a hot-reload changed which persona is active (e.g. the active bundle
    // was deleted/renamed), notify downstream so voice/system-prompt update too.
    if (cur.name != prev_active)
        emit activeChanged(QString::fromStdString(cur.name),
                           QString::fromStdString(cur.voice));

    scanning_ = false;
}

std::vector<Personality> PersonalityManager::all() const { return personas_; }

const Personality& PersonalityManager::active() const {
    // Guard the window before start()/scanBundles() has populated personas_
    // (e.g. PersonalityModel built during AppController::buildModels(), which
    // runs before personality_->start()). Out-of-bounds here is a hard crash.
    if (active_ < 0 || active_ >= static_cast<int>(personas_.size())) {
        static const Personality kNone;
        return kNone;
    }
    return personas_[active_];
}

bool PersonalityManager::setActive(const std::string& name) {
    for (size_t i = 0; i < personas_.size(); ++i) {
        if (personas_[i].name == name) {
            active_ = static_cast<int>(i);
            persistActive(name);
            PM_INFO("personality: active -> '{}'", name);
            emit activeChanged(QString::fromStdString(name),
                               QString::fromStdString(personas_[i].voice));
            return true;
        }
    }
    PM_WARN("personality: setActive('{}') — no such persona", name);
    return false;
}

// ---------------------------------------------------------------------------
//  database mirror
// ---------------------------------------------------------------------------
void PersonalityManager::syncDatabase() {
    const auto dir = Paths::instance().personalities();

    // Each Database::exec/query is individually mutex-guarded and atomic; we run
    // a short sequence of them here. (We deliberately do NOT take db_.mutex()
    // ourselves — that mutex is non-recursive and is already acquired inside
    // exec/query, so holding it around these calls would deadlock. It's only for
    // callers driving raw sqlite via Database::raw().)

    // Upsert every loaded bundle (keyed by name). is_active is intentionally not
    // touched here — persistActive() owns that flag so we don't fight it.
    for (const auto& p : personas_) {
        const std::string bundle_path =
            (dir / p.name / "persona.json").string();
        db_.exec(
            "INSERT INTO personalities(name,bundle_path,voice,preferred_model,wake_phrase)"
            " VALUES(?1,?2,?3,?4,?5)"
            " ON CONFLICT(name) DO UPDATE SET"
            "   bundle_path=excluded.bundle_path,"
            "   voice=excluded.voice,"
            "   preferred_model=excluded.preferred_model,"
            "   wake_phrase=excluded.wake_phrase",
            {p.name, bundle_path, p.voice, p.preferred_model, p.wake_phrase});
    }

    // Prune rows whose bundle has been removed from disk so the table reflects
    // exactly what's installed.
    std::vector<std::string> stale;
    db_.query("SELECT name FROM personalities", {}, [&](const Row& r) {
        const std::string name = r.text(0);
        const bool present = std::any_of(
            personas_.begin(), personas_.end(),
            [&](const Personality& p) { return p.name == name; });
        if (!present) stale.push_back(name);
    });
    for (const auto& name : stale) {
        db_.exec("DELETE FROM personalities WHERE name=?1", {name});
        PM_INFO("personality: pruned removed bundle '{}' from registry", name);
    }
}

int PersonalityManager::resolveActiveIndex() const {
    // Prefer the persona the DB already marks active (persisted choice).
    std::string active_name;
    db_.query("SELECT name FROM personalities WHERE is_active=1 LIMIT 1", {},
              [&](const Row& r) { active_name = r.text(0); });

    if (!active_name.empty()) {
        for (size_t i = 0; i < personas_.size(); ++i)
            if (personas_[i].name == active_name) return static_cast<int>(i);
    }
    return 0;   // default to the first persona
}

void PersonalityManager::persistActive(const std::string& name) {
    // Two atomic statements (no outer db_.mutex(): exec locks it internally and
    // it is non-recursive). A brief window between them is harmless — readers
    // tolerate 0 or 1 active rows and we converge to exactly one.
    db_.exec("UPDATE personalities SET is_active=0 WHERE is_active<>0");
    db_.exec("UPDATE personalities SET is_active=1 WHERE name=?1", {name});
}

// ---------------------------------------------------------------------------
//  hot-reload watcher
// ---------------------------------------------------------------------------
void PersonalityManager::installWatcher() {
    if (watcher_) return;
    watcher_ = std::make_unique<QFileSystemWatcher>(this);

    // A change to the dir (bundle added/removed) or to any watched persona.json
    // (bundle edited) schedules a debounced rescan.
    connect(watcher_.get(), &QFileSystemWatcher::directoryChanged,
            this, [this](const QString&) { onDirChanged(); });
    connect(watcher_.get(), &QFileSystemWatcher::fileChanged,
            this, [this](const QString&) { onDirChanged(); });

    rewatch();
}

void PersonalityManager::rewatch() {
    if (!watcher_) return;

    // Drop everything and re-add: some editors replace files (rename-over), which
    // silently removes them from the watch list, and bundles come and go.
    const QStringList olddirs  = watcher_->directories();
    const QStringList oldfiles = watcher_->files();
    if (!olddirs.isEmpty())  watcher_->removePaths(olddirs);
    if (!oldfiles.isEmpty()) watcher_->removePaths(oldfiles);

    const auto dir = Paths::instance().personalities();
    const QString qdir = QString::fromStdWString(dir.wstring());
    if (QDir(qdir).exists())
        watcher_->addPath(qdir);

    // Also watch each persona.json directly so in-place edits (which may not
    // bump the directory mtime) are caught.
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            const auto file = entry.path() / "persona.json";
            if (fs::exists(file))
                watcher_->addPath(QString::fromStdWString(file.wstring()));
        }
    }
}

void PersonalityManager::onDirChanged() {
    // Refresh the watch set immediately (paths may have appeared/vanished) but
    // defer the actual rescan so a burst of writes coalesces into one reload.
    rewatch();
    rescan_timer_.start();   // restarts the single-shot debounce
}

// ---------------------------------------------------------------------------
//  write API (overhaul2 E2: in-GUI personality editor)
// ---------------------------------------------------------------------------
bool PersonalityManager::createBundle(QString qname) {
    const std::string name = qname.trimmed().toStdString();
    if (name.empty()) return false;
    const std::string folder = sanitizeBundleFolder(name);
    if (folder.empty()) return false;

    const auto dir = Paths::instance().personalities();
    const auto bundleDir = dir / folder;
    std::error_code ec;
    if (fs::exists(bundleDir, ec)) {
        PM_WARN("personality: createBundle('{}') — bundle already exists", name);
        return false;
    }
    fs::create_directories(bundleDir, ec);
    if (ec) {
        PM_WARN("personality: createBundle('{}') — mkdir failed: {}", name, ec.message());
        return false;
    }

    nlohmann::json j;
    j["name"]            = name;
    j["system_prompt"]   = "You are " + name + ", a helpful local home assistant.";
    j["voice"]           = "";
    j["preferred_model"] = "fast";
    j["wake_phrase"]     = "";
    j["tools"]           = nlohmann::json::array();
    j["sampling"] = {
        {"temperature",    0.7},
        {"top_p",          0.9},
        {"top_k",          40},
        {"repeat_penalty", 1.1},
        {"max_tokens",     1024},
    };

    if (!writeJsonAtomic(bundleDir / "persona.json", j)) {
        PM_WARN("personality: createBundle('{}') — write failed", name);
        std::error_code rmec;
        fs::remove_all(bundleDir, rmec);
        return false;
    }

    PM_INFO("personality: created bundle '{}' ({})", name, folder);
    scanBundles();
    return true;
}

bool PersonalityManager::saveBundle(QString qname, QString qjson) {
    const std::string name = qname.trimmed().toStdString();
    if (name.empty()) return false;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(qjson.toStdString());
    } catch (const std::exception& e) {
        PM_WARN("personality: saveBundle('{}') — invalid JSON: {}", name, e.what());
        return false;
    }
    if (!j.is_object()) {
        PM_WARN("personality: saveBundle('{}') — JSON root must be an object", name);
        return false;
    }
    // The bundle's `name` field is authoritative from the argument, not
    // whatever the caller's JSON blob happened to carry.
    j["name"] = name;

    // Reuse the existing on-disk folder if this persona is already loaded
    // (keeps shipped kebab-case-folder bundles stable across edits); else
    // this is effectively create-on-save, so derive a fresh folder.
    std::string folder;
    for (const auto& p : personas_)
        if (p.name == name) { folder = p.bundle_dir; break; }
    if (folder.empty()) folder = sanitizeBundleFolder(name);
    if (folder.empty()) return false;

    const auto dir = Paths::instance().personalities();
    const auto bundleDir = dir / folder;
    std::error_code ec;
    fs::create_directories(bundleDir, ec);

    if (!writeJsonAtomic(bundleDir / "persona.json", j)) {
        PM_WARN("personality: saveBundle('{}') — write failed", name);
        return false;
    }

    PM_INFO("personality: saved bundle '{}'", name);
    scanBundles();
    return true;
}

bool PersonalityManager::setAvatar(QString qname, QString qsourcePath) {
    const std::string name = qname.trimmed().toStdString();
    const std::string source = qsourcePath.trimmed().toStdString();
    if (name.empty() || source.empty()) return false;

    std::error_code ec;
    const fs::path src = fs::u8path(source);
    if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec)) {
        PM_WARN("personality: setAvatar('{}') — source not found: {}", name, source);
        return false;
    }
    std::string ext = src.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") {
        PM_WARN("personality: setAvatar('{}') — unsupported image type: {}", name, ext);
        return false;
    }

    std::string folder;
    for (const auto& p : personas_)
        if (p.name == name) { folder = p.bundle_dir; break; }
    if (folder.empty()) folder = sanitizeBundleFolder(name);
    if (folder.empty()) return false;

    const auto dir = Paths::instance().personalities();
    const auto bundleDir = dir / folder;
    if (!fs::exists(bundleDir, ec)) {
        PM_WARN("personality: setAvatar('{}') — bundle does not exist yet: {}", name, folder);
        return false;
    }

    // Drop any previous avatar of a *different* extension so scanBundles()'s
    // fixed-order probe (png, then jpg, then jpeg) can't resolve a stale file
    // instead of the one just imported.
    for (const char* fname : {"avatar.png", "avatar.jpg", "avatar.jpeg"})
        fs::remove(bundleDir / fname, ec);

    fs::copy_file(src, bundleDir / ("avatar" + ext), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        PM_WARN("personality: setAvatar('{}') — copy failed: {}", name, ec.message());
        return false;
    }

    PM_INFO("personality: set avatar for '{}' (avatar{})", name, ext);
    scanBundles();
    return true;
}

bool PersonalityManager::deleteBundle(QString qname) {
    const std::string name = qname.trimmed().toStdString();
    if (name.empty()) return false;

    if (active_ >= 0 && active_ < static_cast<int>(personas_.size()) &&
        personas_[active_].name == name) {
        PM_WARN("personality: deleteBundle('{}') refused — persona is active, switch first", name);
        return false;
    }

    std::string folder;
    for (const auto& p : personas_)
        if (p.name == name) { folder = p.bundle_dir; break; }
    if (folder.empty()) folder = sanitizeBundleFolder(name);
    if (folder.empty()) return false;

    const auto dir = Paths::instance().personalities();
    const auto bundleDir = dir / folder;
    std::error_code ec;
    if (!fs::exists(bundleDir, ec)) {
        PM_WARN("personality: deleteBundle('{}') — no such bundle on disk", name);
        return false;
    }

    const auto trashDir = dir / ".trash";
    fs::create_directories(trashDir, ec);
    const int64_t ts = to_unix(Clock::now());
    const auto dest = trashDir / (folder + "-" + std::to_string(ts));
    fs::rename(bundleDir, dest, ec);
    if (ec) {
        PM_WARN("personality: deleteBundle('{}') — move to trash failed: {}", name, ec.message());
        return false;
    }

    PM_INFO("personality: moved bundle '{}' to trash ({})", name, dest.string());
    scanBundles();
    return true;
}

} // namespace polymath
