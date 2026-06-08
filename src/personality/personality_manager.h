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
#include <string>
#include <vector>

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
};

class PersonalityManager : public QObject, public IService {
    Q_OBJECT
public:
    explicit PersonalityManager(Database& db, QObject* parent = nullptr);

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
    Database&                db_;
    std::vector<Personality> personas_;
    int                      active_ = 0;
};

} // namespace polymath
