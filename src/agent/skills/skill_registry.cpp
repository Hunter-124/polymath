#include "skill_registry.h"
#include "logging.h"

#include <QDir>
#include <QFileSystemWatcher>
#include <QString>
#include <QStringList>

#include <fstream>
#include <sstream>

namespace polymath {

namespace fs = std::filesystem;

namespace {

constexpr int kRescanDebounceMs = 400;

// Does `dir` contain at least one valid-looking <name>/skill.json?
bool hasAnySkillBundle(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        std::error_code fec;
        if (entry.is_directory(fec) && fs::exists(entry.path() / "skill.json", fec))
            return true;
    }
    return false;
}

fs::path locateStarterSkills() {
#ifdef POLYMATH_SKILLS_DIR
    {
        const fs::path cand(POLYMATH_SKILLS_DIR);
        std::error_code ec;
        if (hasAnySkillBundle(cand)) return cand;
    }
#endif
    return {};
}

int copyStarterSkills(const fs::path& src, const fs::path& dest) {
    std::error_code ec;
    fs::create_directories(dest, ec);
    int copied = 0;
    for (auto& entry : fs::directory_iterator(src, ec)) {
        std::error_code fec;
        if (!entry.is_directory(fec)) continue;
        const auto skill_file = entry.path() / "skill.json";
        if (!fs::exists(skill_file, fec)) continue;
        const auto target_dir = dest / entry.path().filename();
        if (fs::exists(target_dir / "skill.json", fec)) continue;  // don't overwrite
        fs::create_directories(target_dir, fec);
        fs::copy_file(skill_file, target_dir / "skill.json",
                      fs::copy_options::skip_existing, fec);
        if (!fec) {
            ++copied;
            PM_INFO("skills: seeded starter '{}'", entry.path().filename().string());
        }
    }
    return copied;
}

} // namespace

SkillRegistry::SkillRegistry(QObject* parent)
    : QObject(parent) {
    rescan_timer_.setSingleShot(true);
    rescan_timer_.setInterval(kRescanDebounceMs);
    connect(&rescan_timer_, &QTimer::timeout, this, [this] { load(); });
}

SkillRegistry::~SkillRegistry() {
    stopWatcher();
}

void SkillRegistry::setSkillsDir(const fs::path& dir) {
    skills_dir_ = dir;
}

int SkillRegistry::seedStartersIfEmpty() {
    if (skills_dir_.empty()) return 0;
    std::error_code ec;
    fs::create_directories(skills_dir_, ec);
    if (hasAnySkillBundle(skills_dir_)) return 0;
    const fs::path src = locateStarterSkills();
    if (src.empty()) {
        PM_WARN("skills: no starter skills found to seed (POLYMATH_SKILLS_DIR unset/missing)");
        return 0;
    }
    return copyStarterSkills(src, skills_dir_);
}

void SkillRegistry::load() {
    if (loading_) return;
    loading_ = true;

    std::map<std::string, Skill> loaded;
    if (skills_dir_.empty()) {
        PM_WARN("skills: load() with empty skills dir");
        skills_.swap(loaded);
        loading_ = false;
        emit skillsChanged();
        return;
    }

    std::error_code ec;
    if (!fs::exists(skills_dir_, ec)) {
        fs::create_directories(skills_dir_, ec);
    }

    if (fs::is_directory(skills_dir_, ec)) {
        for (auto& entry : fs::directory_iterator(skills_dir_, ec)) {
            std::error_code fec;
            if (!entry.is_directory(fec)) continue;
            const auto file = entry.path() / "skill.json";
            if (!fs::exists(file, fec)) continue;

            auto v = loadSkillFile(file);
            if (!v.ok) {
                PM_WARN("skills: skip '{}': {}", file.string(), v.error);
                continue;
            }
            // Prefer directory name as canonical key when it differs? Spec uses
            // name field; warn on mismatch but accept the JSON name.
            const std::string dir_name = entry.path().filename().string();
            if (v.skill.name != dir_name) {
                PM_WARN("skills: name '{}' != directory '{}' (using JSON name)",
                        v.skill.name, dir_name);
            }
            if (loaded.count(v.skill.name)) {
                PM_WARN("skills: duplicate name '{}', keeping first", v.skill.name);
                continue;
            }
            loaded.emplace(v.skill.name, std::move(v.skill));
        }
    }

    skills_.swap(loaded);
    PM_INFO("skills: loaded {} skill(s) from {}", skills_.size(), skills_dir_.string());
    loading_ = false;

    if (watcher_) rewatch();
    emit skillsChanged();
}

void SkillRegistry::installWatcher() {
    if (watcher_) return;
    if (skills_dir_.empty()) return;

    watcher_ = std::make_unique<QFileSystemWatcher>(this);
    connect(watcher_.get(), &QFileSystemWatcher::directoryChanged,
            this, [this](const QString&) { onFsChanged(); });
    connect(watcher_.get(), &QFileSystemWatcher::fileChanged,
            this, [this](const QString&) { onFsChanged(); });
    rewatch();
}

void SkillRegistry::stopWatcher() {
    rescan_timer_.stop();
    if (!watcher_) return;
    const QStringList dirs  = watcher_->directories();
    const QStringList files = watcher_->files();
    if (!dirs.isEmpty())  watcher_->removePaths(dirs);
    if (!files.isEmpty()) watcher_->removePaths(files);
    watcher_.reset();
}

std::vector<Skill> SkillRegistry::all() const {
    std::vector<Skill> out;
    out.reserve(skills_.size());
    for (const auto& kv : skills_) out.push_back(kv.second);
    return out;
}

const Skill* SkillRegistry::get(const std::string& name) const {
    auto it = skills_.find(name);
    return it == skills_.end() ? nullptr : &it->second;
}

bool SkillRegistry::has(const std::string& name) const {
    return skills_.count(name) > 0;
}

nlohmann::json SkillRegistry::catalog() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& kv : skills_)
        arr.push_back(skillCatalogEntry(kv.second));
    return arr;
}

std::string SkillRegistry::catalogText() const {
    std::ostringstream oss;
    for (const auto& kv : skills_) {
        const auto& s = kv.second;
        oss << "- " << s.name << ": " << s.description;
        if (!s.triggers.empty()) {
            oss << " [";
            for (size_t i = 0; i < s.triggers.size(); ++i) {
                if (i) oss << ", ";
                oss << s.triggers[i];
            }
            oss << "]";
        }
        oss << "\n";
    }
    return oss.str();
}

nlohmann::json SkillRegistry::expand(const std::string& name,
                                     const nlohmann::json& params) const {
    const Skill* s = get(name);
    if (!s) return {{"error", "unknown skill: " + name}};
    return expandSkillToGoal(*s, params);
}

bool SkillRegistry::saveSkill(nlohmann::json skill_json, bool force_confirm,
                              std::string* err) {
    auto set_err = [&](const std::string& e) {
        if (err) *err = e;
        return false;
    };
    if (skills_dir_.empty())
        return set_err("skills directory not set");
    if (!skill_json.is_object())
        return set_err("skill must be a JSON object");

    if (force_confirm)
        skill_json["confirm"] = true;

    auto v = validateSkillJson(skill_json);
    if (!v.ok)
        return set_err(v.error);

    const fs::path dir = skills_dir_ / v.skill.name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
        return set_err("cannot create skill dir: " + ec.message());

    const fs::path file = dir / "skill.json";
    try {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out)
            return set_err("cannot write " + file.string());
        // Pretty-print for user editability.
        out << skill_json.dump(2);
        if (!out)
            return set_err("write failed: " + file.string());
    } catch (const std::exception& e) {
        return set_err(std::string("write error: ") + e.what());
    }

    PM_INFO("skills: saved skill '{}' (confirm={}) to {}",
            v.skill.name, force_confirm ? "true" : "false", file.string());
    load();
    return true;
}

void SkillRegistry::rewatch() {
    if (!watcher_ || skills_dir_.empty()) return;

    const QStringList olddirs  = watcher_->directories();
    const QStringList oldfiles = watcher_->files();
    if (!olddirs.isEmpty())  watcher_->removePaths(olddirs);
    if (!oldfiles.isEmpty()) watcher_->removePaths(oldfiles);

    const QString qdir = QString::fromStdWString(skills_dir_.wstring());
    if (QDir(qdir).exists())
        watcher_->addPath(qdir);

    std::error_code ec;
    if (fs::exists(skills_dir_, ec)) {
        for (auto& entry : fs::directory_iterator(skills_dir_, ec)) {
            const auto file = entry.path() / "skill.json";
            if (fs::exists(file))
                watcher_->addPath(QString::fromStdWString(file.wstring()));
        }
    }
}

void SkillRegistry::onFsChanged() {
    rewatch();
    rescan_timer_.start();
}

} // namespace polymath
