#pragma once
//
// SkillRegistry — load / validate / watch data/skills/<name>/skill.json and
// expose a catalog + expand-to-goal API for the harness router and run_skill
// tool (overhaul 03 §4).
//
#include "skill.h"

#include <QObject>
#include <QTimer>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

class QFileSystemWatcher;

namespace polymath {

class SkillRegistry : public QObject {
    Q_OBJECT
public:
    explicit SkillRegistry(QObject* parent = nullptr);
    ~SkillRegistry() override;

    // Directory that holds <name>/skill.json bundles. Must be set before load().
    void setSkillsDir(const std::filesystem::path& dir);
    const std::filesystem::path& skillsDir() const { return skills_dir_; }

    // If skills_dir_ is empty of valid skills, copy starter bundles from the
    // compile-time POLYMATH_SKILLS_DIR (data/skills in the source tree).
    // Returns number of bundles copied.
    int seedStartersIfEmpty();

    // (Re)scan skills_dir_, validate each skill.json, replace in-memory map.
    // Invalid files are skipped (logged); previously-good skills with the same
    // name are dropped if their file is gone or now invalid.
    void load();

    // Install a debounced QFileSystemWatcher on skills_dir_ + each skill.json.
    // Safe to call multiple times (no-op if already installed).
    void installWatcher();
    void stopWatcher();

    std::vector<Skill> all() const;
    const Skill* get(const std::string& name) const;
    bool has(const std::string& name) const;
    size_t size() const { return skills_.size(); }

    // Array of compact catalog entries for the router prompt / tool docs.
    nlohmann::json catalog() const;
    // Multi-line human-readable catalog (name — description [triggers]).
    std::string catalogText() const;

    // Expand skill `name` with `params` → goal-shaped JSON (see expandSkillToGoal).
    // On unknown skill: { "error": "unknown skill: ..." }.
    nlohmann::json expand(const std::string& name, const nlohmann::json& params) const;

    // Persist a skill JSON object under skills_dir_/<name>/skill.json.
    // When force_confirm is true (AI-authored via save_skill), confirm is forced
    // to true in the written file. Reloads the registry on success.
    // Returns false and sets *err on failure.
    bool saveSkill(nlohmann::json skill_json, bool force_confirm, std::string* err = nullptr);

signals:
    void skillsChanged();

private:
    void rewatch();
    void onFsChanged();

    std::filesystem::path               skills_dir_;
    std::map<std::string, Skill>        skills_;
    std::unique_ptr<QFileSystemWatcher> watcher_;
    QTimer                              rescan_timer_;
    bool                                loading_ = false;
};

} // namespace polymath
