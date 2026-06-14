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
    std::string avatar_path;          // auto-discovered avatar.png/jpg (legacy)

    // --- visual avatar ("the face" shown + animated in the UI; all optional) ---
    // Driven by an optional `avatar` object in persona.json:
    //   "avatar": { "style": "orb|bars|ring|image", "accent": "#7aa2f7",
    //               "idle": "idle.gif", "talking": "talking.gif" }
    // With nothing set, the UI draws a procedural orb tinted by the theme accent.
    std::string avatar_style;         // "orb" (default) | "bars" | "ring" | "image"
    std::string avatar_accent;        // hex tint ("" -> theme accent)
    std::string avatar_idle;          // resolved path to an idle image/GIF ("" -> procedural)
    std::string avatar_talking;       // resolved path to a talking image/GIF
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
