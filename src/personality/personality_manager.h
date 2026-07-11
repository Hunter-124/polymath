#pragma once
//
// PersonalityManager — loads modular persona bundles from personalities/<name>/
// persona.json (hot-reloadable; drop a folder in to add one).  The active
// personality supplies the system prompt, TTS voice, sampling params, preferred
// model, optional wake phrase, and tool allow-list used by the AgentRuntime.
//
#include "service.h"
#include "types.h"
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>
#include <string>
#include <vector>

class QFileSystemWatcher;

namespace polymath {

class Database;

struct Personality {
    std::string name;
    std::string system_prompt;
    std::string voice;             // Piper voice id
    std::string preferred_model;   // "fast" | registry id
    std::string wake_phrase;
    SamplingParams sampling;
    std::vector<std::string> tools;   // allow-list ("" -> all)
    std::string avatar_path;
    // On-disk folder name under personalities/ (e.g. "marcus-aurelius" for a
    // persona whose display `name` is "Marcus Aurelius"). The shipped starter
    // bundles use kebab-case folders that don't match their display name, so
    // the E2 write API needs this to resolve save/delete/avatar targets to the
    // *actual* directory instead of re-deriving one from `name` and creating a
    // duplicate. Populated by scanBundles(); empty on the built-in fallback.
    std::string bundle_dir;
};

class PersonalityManager : public QObject, public IService {
    Q_OBJECT
public:
    explicit PersonalityManager(Database& db, QObject* parent = nullptr);
    ~PersonalityManager() override;   // out-of-line: unique_ptr<QFileSystemWatcher>

    void start() override;
    void stop() override;
    const char* serviceName() const override { return "personality"; }

    void scanBundles();                       // (re)load personalities/ dir
    std::vector<Personality> all() const;
    const Personality& active() const;
    bool setActive(const std::string& name);

    // --- write API (overhaul2 E2: in-GUI personality editor) ---------------
    // These are additive — the methods above stay frozen exactly as-is.

    // Scaffolds personalities/<name>/persona.json from a sensible template
    // (name/system_prompt seeded, empty voice/wake phrase, "fast" preferred
    // model, no tool restriction, default sampling). Fails (false) if `name`
    // is empty/unsafe as a path segment or a bundle already exists on disk
    // under it. Rescans synchronously so the new card appears immediately
    // (the QFileSystemWatcher would also catch it, just not before the QML
    // caller's next paint).
    Q_INVOKABLE bool createBundle(QString name);
    // Atomic (tmp file + rename) overwrite of the bundle's persona.json.
    // `json` must parse as a JSON object; the "name" field is forced to match
    // `name`. Writes into the existing bundle's on-disk folder when `name`
    // matches an already-loaded persona (so editing a shipped kebab-case
    // bundle doesn't fork a duplicate folder); otherwise derives a fresh
    // folder from `name` (create-on-save for a persona new this session).
    Q_INVOKABLE bool saveBundle(QString name, QString json);
    // Copies `sourcePath` (an arbitrary absolute path, e.g. pasted from
    // Explorer — no native file-picker QML module is linked yet) into the
    // bundle as avatar.<ext> (png/jpg/jpeg only), replacing any previous
    // avatar of a different extension. Requires the bundle to already exist.
    Q_INVOKABLE bool setAvatar(QString name, QString sourcePath);
    // Moves personalities/<name>/ to personalities/.trash/<folder>-<epoch>/ —
    // reversible, never a hard delete. Refuses (returns false, no-op) when
    // `name` is the currently active persona; the caller must switch personas
    // first.
    Q_INVOKABLE bool deleteBundle(QString name);

signals:
    void activeChanged(QString name, QString voice);
    void personalitiesChanged();

private:
    // Mirror personas_ into the `personalities` table (upsert + prune), keeping
    // is_active consistent so the agent module can read the active persona.
    void syncDatabase();
    // Restore the previously-active persona by name (survives restarts); returns
    // the resolved index (0 if the stored name is gone / nothing persisted).
    int  resolveActiveIndex() const;
    // Persist exactly one is_active=1 row for `name` (all others cleared).
    void persistActive(const std::string& name);
    // Watch personalities/ for added/removed/edited bundles; debounced rescan.
    void installWatcher();
    void rewatch();              // (re)register the dir + per-bundle paths
    void onDirChanged();         // QFileSystemWatcher hook -> schedules a rescan

    Database&                          db_;
    std::vector<Personality>           personas_;
    int                                active_ = 0;
    std::unique_ptr<QFileSystemWatcher> watcher_;
    QTimer                             rescan_timer_;   // debounce filesystem churn
    bool                               scanning_ = false;
};

} // namespace polymath
