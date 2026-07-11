#pragma once
//
// PersonalityModel — a QAbstractListModel view over PersonalityManager::all(),
// mirroring the ShoppingModel pattern (03 Wave-3 UI data layer) so the QML
// personality editor (overhaul2 E2) can bind a ListView directly instead of
// re-deriving state from AppController::personalities()' QStringList.
//
// Read-only mirror: mutations go through AppController's pass-through
// invokables (createPersonality/savePersonality/deletePersonality/
// setPersonalityAvatar), which call PersonalityManager's write API. That
// write API always ends in scanBundles(), which emits personalitiesChanged()
// (and activeChanged() on an active-persona swap) — this model just listens
// for those and reloads its in-memory rows from PersonalityManager::all().
//
#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace polymath {

class PersonalityManager;

class PersonalityModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        SystemPromptRole,
        VoiceRole,
        PreferredModelRole,
        WakePhraseRole,
        ToolsRole,
        TemperatureRole,
        TopPRole,
        TopKRole,
        RepeatPenaltyRole,
        MaxTokensRole,
        AvatarPathRole,
        IsActiveRole,
    };

    explicit PersonalityModel(PersonalityManager& mgr, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Reload every row from PersonalityManager::all() (cheap: an in-memory
    // vector copy, no I/O — the manager already owns the parsed bundles).
    Q_INVOKABLE void refresh();

    // Row lookup by display name (-1 if not found) — the editor addresses
    // personas by name, not row index, so it can be opened straight from a
    // QML delegate's `name` role.
    Q_INVOKABLE int indexOfName(const QString& name) const;

    // Snapshot of one row as a plain map (all roles by their QML name), for
    // seeding the editor's local edit-state without holding a live model
    // index across an edit session.
    Q_INVOKABLE QVariantMap get(int row) const;

private:
    struct Row {
        QString     name;
        QString     systemPrompt;
        QString     voice;
        QString     preferredModel;
        QString     wakePhrase;
        QStringList tools;
        double      temperature    = 0.7;
        double      topP           = 0.9;
        int         topK           = 40;
        double      repeatPenalty  = 1.1;
        int         maxTokens      = 1024;
        QString     avatarPath;
        bool        isActive       = false;
    };

    PersonalityManager& mgr_;
    QVector<Row>        rows_;
};

} // namespace polymath
