#pragma once
//
// ChatModel — the conversation log shown in ChatView.  Unlike the other models
// this one is not backed by a SQLite table: a turn-by-turn chat history is
// transient UI state.  Rows are appended by the view (user text) and by
// AppController forwarding EventBus::tokenStreamed (assistant tokens), which
// this model coalesces per request_id into a single streaming assistant bubble.
//
#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

namespace polymath {

class ChatModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        WhoRole = Qt::UserRole + 1,   // "you" | "assistant"
        TextRole,
        StreamingRole,                // true while the assistant bubble is still filling
        RequestIdRole
    };

    explicit ChatModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // --- mutation API (called on the UI thread) ---

    // Append a user message and return the request_id that will correlate the
    // assistant's streamed reply.  The caller passes this to AppController::sendText
    // via the QML layer; the matching assistant bubble is created lazily on the
    // first token for that id (so out-of-order ids still render correctly).
    Q_INVOKABLE void appendUser(const QString& text);

    // Feed a streamed assistant token.  Creates the assistant bubble for
    // request_id on first sight, appends text, and marks it finished on done.
    Q_INVOKABLE void appendAssistantToken(const QString& request_id,
                                          const QString& text, bool done);

    Q_INVOKABLE void clear();

private:
    struct Turn {
        QString who;
        QString text;
        QString request_id;
        bool    streaming = false;
    };

    int indexForRequest(const QString& request_id) const;  // assistant row, or -1

    QVector<Turn> turns_;
};

} // namespace polymath
