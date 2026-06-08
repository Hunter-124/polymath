#include "personality_manager.h"
#include "database.h"
#include "paths.h"
#include "logging.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

// Mostly-real implementation (no native deps): loads persona.json bundles. The
// Wave-2 personality agent adds avatar handling, hot-reload watching, validation,
// and DB sync of the `personalities` table.

namespace polymath {

PersonalityManager::PersonalityManager(Database& db, QObject* parent)
    : QObject(parent), db_(db) {}

void PersonalityManager::start() { scanBundles(); }
void PersonalityManager::stop() {}

void PersonalityManager::scanBundles() {
    namespace fs = std::filesystem;
    personas_.clear();
    const auto dir = Paths::instance().personalities();
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            auto file = entry.path() / "persona.json";
            if (!fs::exists(file)) continue;
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
                if (j.contains("sampling")) {
                    p.sampling.temperature = j["sampling"].value("temperature", 0.7f);
                    p.sampling.top_p       = j["sampling"].value("top_p", 0.9f);
                }
                personas_.push_back(std::move(p));
            } catch (const std::exception& e) {
                PM_WARN("bad persona bundle {}: {}", file.string(), e.what());
            }
        }
    }
    if (personas_.empty())
        personas_.push_back({"Assistant", "You are a helpful local home assistant.", "", "fast", "", {}, {}, ""});
    PM_INFO("Loaded {} personalities", personas_.size());
    emit personalitiesChanged();
}

std::vector<Personality> PersonalityManager::all() const { return personas_; }
const Personality& PersonalityManager::active() const { return personas_[active_]; }

bool PersonalityManager::setActive(const std::string& name) {
    for (size_t i = 0; i < personas_.size(); ++i) {
        if (personas_[i].name == name) {
            active_ = (int)i;
            emit activeChanged(QString::fromStdString(name),
                               QString::fromStdString(personas_[i].voice));
            return true;
        }
    }
    return false;
}

} // namespace polymath
