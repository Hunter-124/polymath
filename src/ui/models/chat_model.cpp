#include "chat_model.h"

#include <QDateTime>

namespace polymath {

namespace {
QString nowLabel() {
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
}
} // namespace

ChatModel::ChatModel(QObject* parent) : QAbstractListModel(parent) {}

int ChatModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(turns_.size());
}

QVariant ChatModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= turns_.size())
        return {};
    const Turn& t = turns_.at(index.row());
    switch (role) {
        case WhoRole:       return t.who;
        case TextRole:      return t.text;
        case StreamingRole: return t.streaming;
        case RequestIdRole: return t.request_id;
        case TimeLabelRole: return t.time_label;
        default:            return {};
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const {
    return {
        {WhoRole,       "who"},
        {TextRole,      "text"},
        {StreamingRole, "streaming"},
        {RequestIdRole, "requestId"},
        {TimeLabelRole, "timeLabel"},
    };
}

void ChatModel::appendUser(const QString& text) {
    const int row = static_cast<int>(turns_.size());
    beginInsertRows({}, row, row);
    turns_.push_back(Turn{QStringLiteral("you"), text, {}, false, nowLabel()});
    endInsertRows();
}

int ChatModel::indexForRequest(const QString& request_id) const {
    if (request_id.isEmpty()) return -1;
    // The active assistant bubble for a request is the last "assistant" row whose
    // request_id matches; iterate from the back since it is almost always newest.
    for (int i = static_cast<int>(turns_.size()) - 1; i >= 0; --i) {
        const Turn& t = turns_.at(i);
        if (t.who == QLatin1String("assistant") && t.request_id == request_id)
            return i;
    }
    return -1;
}

void ChatModel::appendAssistantToken(const QString& request_id,
                                     const QString& text, bool done) {
    int row = indexForRequest(request_id);
    if (row < 0) {
        // First token for this request: create the assistant bubble.
        row = static_cast<int>(turns_.size());
        beginInsertRows({}, row, row);
        turns_.push_back(Turn{QStringLiteral("assistant"), text, request_id, !done, nowLabel()});
        endInsertRows();
        if (done) {
            const QModelIndex idx = index(row);
            emit dataChanged(idx, idx, {StreamingRole});
        }
        return;
    }

    Turn& t = turns_[row];
    if (!text.isEmpty()) t.text += text;
    const bool was_streaming = t.streaming;
    if (done) t.streaming = false;

    const QModelIndex idx = index(row);
    QVector<int> changed{TextRole};
    if (was_streaming != t.streaming) changed.push_back(StreamingRole);
    emit dataChanged(idx, idx, changed);
}

void ChatModel::clear() {
    if (turns_.isEmpty()) return;
    beginResetModel();
    turns_.clear();
    endResetModel();
}

} // namespace polymath
