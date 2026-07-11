#include "personality_model.h"

#include "personality_manager.h"

namespace polymath {

PersonalityModel::PersonalityModel(PersonalityManager& mgr, QObject* parent)
    : QAbstractListModel(parent), mgr_(mgr) {
    connect(&mgr_, &PersonalityManager::personalitiesChanged, this, &PersonalityModel::refresh);
    // An active-persona swap alone (no bundle add/remove/edit) doesn't emit
    // personalitiesChanged — but it flips every row's isActive, so refresh here too.
    connect(&mgr_, &PersonalityManager::activeChanged, this, &PersonalityModel::refresh);
    refresh();
}

int PersonalityModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return rows_.size();
}

QVariant PersonalityModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
    const Row& r = rows_.at(index.row());
    switch (role) {
        case NameRole:           return r.name;
        case SystemPromptRole:   return r.systemPrompt;
        case VoiceRole:          return r.voice;
        case PreferredModelRole: return r.preferredModel;
        case WakePhraseRole:     return r.wakePhrase;
        case ToolsRole:          return r.tools;
        case TemperatureRole:    return r.temperature;
        case TopPRole:           return r.topP;
        case TopKRole:           return r.topK;
        case RepeatPenaltyRole:  return r.repeatPenalty;
        case MaxTokensRole:      return r.maxTokens;
        case AvatarPathRole:     return r.avatarPath;
        case IsActiveRole:       return r.isActive;
        default:                 return {};
    }
}

QHash<int, QByteArray> PersonalityModel::roleNames() const {
    return {
        {NameRole,           "name"},
        {SystemPromptRole,   "systemPrompt"},
        {VoiceRole,          "voice"},
        {PreferredModelRole, "preferredModel"},
        {WakePhraseRole,     "wakePhrase"},
        {ToolsRole,          "tools"},
        {TemperatureRole,    "temperature"},
        {TopPRole,           "topP"},
        {TopKRole,           "topK"},
        {RepeatPenaltyRole,  "repeatPenalty"},
        {MaxTokensRole,      "maxTokens"},
        {AvatarPathRole,     "avatarPath"},
        {IsActiveRole,       "isActive"},
    };
}

void PersonalityModel::refresh() {
    beginResetModel();
    rows_.clear();
    const auto all = mgr_.all();
    const std::string activeName = mgr_.active().name;
    rows_.reserve(static_cast<int>(all.size()));
    for (const auto& p : all) {
        Row r;
        r.name           = QString::fromStdString(p.name);
        r.systemPrompt   = QString::fromStdString(p.system_prompt);
        r.voice          = QString::fromStdString(p.voice);
        r.preferredModel = QString::fromStdString(p.preferred_model);
        r.wakePhrase     = QString::fromStdString(p.wake_phrase);
        for (const auto& t : p.tools) r.tools << QString::fromStdString(t);
        r.temperature    = p.sampling.temperature;
        r.topP           = p.sampling.top_p;
        r.topK           = p.sampling.top_k;
        r.repeatPenalty  = p.sampling.repeat_penalty;
        r.maxTokens      = p.sampling.max_tokens;
        r.avatarPath     = QString::fromStdString(p.avatar_path);
        r.isActive       = (p.name == activeName);
        rows_.push_back(std::move(r));
    }
    endResetModel();
}

int PersonalityModel::indexOfName(const QString& name) const {
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_.at(i).name == name) return i;
    return -1;
}

QVariantMap PersonalityModel::get(int row) const {
    QVariantMap m;
    if (row < 0 || row >= rows_.size()) return m;
    const Row& r = rows_.at(row);
    m["name"]           = r.name;
    m["systemPrompt"]   = r.systemPrompt;
    m["voice"]          = r.voice;
    m["preferredModel"] = r.preferredModel;
    m["wakePhrase"]     = r.wakePhrase;
    m["tools"]          = QVariant(r.tools);
    m["temperature"]    = r.temperature;
    m["topP"]           = r.topP;
    m["topK"]           = r.topK;
    m["repeatPenalty"]  = r.repeatPenalty;
    m["maxTokens"]      = r.maxTokens;
    m["avatarPath"]     = r.avatarPath;
    m["isActive"]       = r.isActive;
    return m;
}

} // namespace polymath
