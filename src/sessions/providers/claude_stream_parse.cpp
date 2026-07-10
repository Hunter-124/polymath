#include "claude_stream_parse.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace polymath {
namespace {

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QString extractTextFromContent(const QJsonValue& content) {
    if (content.isString())
        return content.toString();
    if (!content.isArray())
        return {};
    QString out;
    for (const QJsonValue& v : content.toArray()) {
        if (v.isString()) {
            if (!out.isEmpty()) out += QLatin1Char('\n');
            out += v.toString();
            continue;
        }
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text") || type.isEmpty()) {
            const QString t = o.value(QStringLiteral("text")).toString();
            if (!t.isEmpty()) {
                if (!out.isEmpty()) out += QLatin1Char('\n');
                out += t;
            }
        } else if (type == QLatin1String("tool_use") || type == QLatin1String("tool_result")) {
            // Prefer human-readable name / content for NeedsInput detection.
            const QString name = o.value(QStringLiteral("name")).toString();
            const QString c = o.value(QStringLiteral("content")).toString();
            QString piece = name;
            if (!c.isEmpty()) piece = piece.isEmpty() ? c : (piece + QLatin1String(": ") + c);
            // Also flatten input for AskUserQuestion.
            if (o.contains(QStringLiteral("input"))) {
                const QJsonDocument d(o.value(QStringLiteral("input")).toObject());
                const QString input = QString::fromUtf8(d.toJson(QJsonDocument::Compact));
                if (!input.isEmpty() && input != QLatin1String("{}"))
                    piece = piece.isEmpty() ? input : (piece + QLatin1Char(' ') + input);
            }
            if (!piece.isEmpty()) {
                if (!out.isEmpty()) out += QLatin1Char('\n');
                out += piece;
            }
        }
    }
    return out;
}

AgentEvent makeEvent(const QString& sid, AgentEvent::Kind kind,
                     const QString& text, const QString& raw,
                     double cost = 0) {
    AgentEvent e;
    e.session_id = sid;
    e.kind = kind;
    e.text = text;
    e.raw_json = raw;
    e.cost_usd = cost;
    e.ts = nowMs();
    return e;
}

} // namespace

bool looksLikeNeedsInput(const QString& text, const QString& raw_json) {
    const QString hay = (text + QLatin1Char('\n') + raw_json).toLower();
    static const char* needles[] = {
        "permission",
        "allow this",
        "approve",
        "plan approval",
        "askuserquestion",
        "needs your input",
        "waiting for",
        "do you want to",
        "confirm",
        "y/n",
        "(y/n)",
        "yes/no",
        "input required",
        "requires approval",
        "permission denied",
        "tool permission",
    };
    for (const char* n : needles) {
        if (hay.contains(QLatin1String(n)))
            return true;
    }
    // Claude Code tool_use name shapes
    if (raw_json.contains(QLatin1String("AskUserQuestion"), Qt::CaseInsensitive)
        || raw_json.contains(QLatin1String("ExitPlanMode"), Qt::CaseInsensitive)
        || raw_json.contains(QLatin1String("permission"), Qt::CaseInsensitive))
        return true;
    return false;
}

QVector<AgentEvent> parseClaudeStreamLine(const QString& line,
                                          const QString& session_id,
                                          QString* out_native_id) {
    QVector<AgentEvent> out;
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return out;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return out;

    const QJsonObject o = doc.object();
    const QString type = o.value(QStringLiteral("type")).toString();
    const QString subtype = o.value(QStringLiteral("subtype")).toString();

    // system / init — capture native session id
    if (type == QLatin1String("system")
        || (type == QLatin1String("system") && subtype == QLatin1String("init"))
        || subtype == QLatin1String("init")) {
        QString native = o.value(QStringLiteral("session_id")).toString();
        if (native.isEmpty())
            native = o.value(QStringLiteral("sessionId")).toString();
        if (!native.isEmpty() && out_native_id)
            *out_native_id = native;
        const QString model = o.value(QStringLiteral("model")).toString();
        AgentEvent e = makeEvent(session_id, AgentEvent::Started,
                                 model.isEmpty()
                                     ? QStringLiteral("session started")
                                     : QStringLiteral("session started (%1)").arg(model),
                                 trimmed);
        e.native_session_id = native;
        out.push_back(std::move(e));
        return out;
    }

    // assistant message
    if (type == QLatin1String("assistant") || type == QLatin1String("message")) {
        QString text;
        if (o.contains(QStringLiteral("message")) && o.value(QStringLiteral("message")).isObject()) {
            const QJsonObject msg = o.value(QStringLiteral("message")).toObject();
            text = extractTextFromContent(msg.value(QStringLiteral("content")));
            // Detect tool_use blocks that need user input
            const QJsonValue content = msg.value(QStringLiteral("content"));
            if (content.isArray()) {
                for (const QJsonValue& v : content.toArray()) {
                    if (!v.isObject()) continue;
                    const QJsonObject block = v.toObject();
                    const QString btype = block.value(QStringLiteral("type")).toString();
                    const QString name = block.value(QStringLiteral("name")).toString();
                    if (btype == QLatin1String("tool_use")
                        && (name.contains(QLatin1String("AskUser"), Qt::CaseInsensitive)
                            || name.contains(QLatin1String("Permission"), Qt::CaseInsensitive)
                            || name == QLatin1String("ExitPlanMode"))) {
                        out.push_back(makeEvent(session_id, AgentEvent::NeedsInput,
                                                text.isEmpty() ? name : text, trimmed));
                        return out;
                    }
                    if (btype == QLatin1String("tool_use")) {
                        out.push_back(makeEvent(session_id, AgentEvent::ToolUse,
                                                name.isEmpty() ? QStringLiteral("tool") : name,
                                                trimmed));
                        // continue — may also have text
                    }
                }
            }
        } else {
            text = extractTextFromContent(o.value(QStringLiteral("content")));
            if (text.isEmpty())
                text = o.value(QStringLiteral("text")).toString();
        }
        if (looksLikeNeedsInput(text, trimmed)) {
            out.push_back(makeEvent(session_id, AgentEvent::NeedsInput, text, trimmed));
            return out;
        }
        if (!text.isEmpty())
            out.push_back(makeEvent(session_id, AgentEvent::AssistantText, text, trimmed));
        return out;
    }

    // result
    if (type == QLatin1String("result")) {
        const bool is_error = o.value(QStringLiteral("is_error")).toBool(false)
                           || subtype == QLatin1String("error")
                           || subtype == QLatin1String("failure");
        QString text = o.value(QStringLiteral("result")).toString();
        if (text.isEmpty())
            text = o.value(QStringLiteral("error")).toString();
        if (text.isEmpty())
            text = extractTextFromContent(o.value(QStringLiteral("content")));
        double cost = o.value(QStringLiteral("total_cost_usd")).toDouble(0);
        if (cost == 0)
            cost = o.value(QStringLiteral("cost_usd")).toDouble(0);
        if (is_error) {
            out.push_back(makeEvent(session_id, AgentEvent::Error,
                                    text.isEmpty() ? QStringLiteral("error") : text,
                                    trimmed, cost));
        } else if (looksLikeNeedsInput(text, trimmed)) {
            out.push_back(makeEvent(session_id, AgentEvent::NeedsInput, text, trimmed, cost));
        } else {
            out.push_back(makeEvent(session_id, AgentEvent::Result,
                                    text.isEmpty() ? QStringLiteral("done") : text,
                                    trimmed, cost));
        }
        if (cost > 0)
            out.push_back(makeEvent(session_id, AgentEvent::CostUpdate, {}, trimmed, cost));
        return out;
    }

    // error / permission shapes
    if (type == QLatin1String("error") || type == QLatin1String("exception")) {
        const QString text = o.value(QStringLiteral("error")).toString(
            o.value(QStringLiteral("message")).toString(QStringLiteral("error")));
        out.push_back(makeEvent(session_id, AgentEvent::Error, text, trimmed));
        return out;
    }

    if (type == QLatin1String("user") || type == QLatin1String("permission_request")
        || type == QLatin1String("control_request")) {
        QString text;
        if (o.contains(QStringLiteral("message"))) {
            const QJsonValue msg = o.value(QStringLiteral("message"));
            if (msg.isObject())
                text = extractTextFromContent(msg.toObject().value(QStringLiteral("content")));
            else if (msg.isString())
                text = msg.toString();
        }
        if (text.isEmpty())
            text = o.value(QStringLiteral("prompt")).toString(
                o.value(QStringLiteral("text")).toString());
        if (looksLikeNeedsInput(text, trimmed)
            || type == QLatin1String("permission_request")
            || type == QLatin1String("control_request")) {
            out.push_back(makeEvent(session_id, AgentEvent::NeedsInput,
                                    text.isEmpty() ? QStringLiteral("permission / input required")
                                                   : text,
                                    trimmed));
            return out;
        }
    }

    // thinking / stream partials
    if (type == QLatin1String("thinking")
        || type == QLatin1String("content_block_delta")
        || subtype == QLatin1String("thinking")) {
        const QString text = o.value(QStringLiteral("text")).toString(
            o.value(QStringLiteral("thinking")).toString());
        out.push_back(makeEvent(session_id, AgentEvent::Thinking, text, trimmed));
        return out;
    }

    return out;
}

QVector<AgentEvent> parseClaudeStreamTranscript(const QString& body,
                                                const QString& session_id,
                                                QString* out_native_id) {
    QVector<AgentEvent> all;
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const auto evs = parseClaudeStreamLine(line, session_id, out_native_id);
        for (const auto& e : evs)
            all.push_back(e);
    }
    return all;
}

} // namespace polymath
