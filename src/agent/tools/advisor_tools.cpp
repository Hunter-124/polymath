#include "advisor_tools.h"
#include "config.h"
#include "database.h"
#include "logging.h"
#include "paths.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTime>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace polymath {

namespace {

// Minimal ICS VEVENT parser: SUMMARY + DTSTART (+ optional DTEND).
struct CalEvent {
    std::string summary;
    std::string start;  // raw DTSTART value
    std::string end;
    qint64 startUnix = 0;
};

qint64 parseIcsDate(const QString& raw) {
    // Forms: 20260711T150000Z, 20260711T150000, 20260711
    QString s = raw.trimmed();
    if (s.startsWith(QLatin1String("TZID="))) {
        const int colon = s.indexOf(QLatin1Char(':'));
        if (colon > 0) s = s.mid(colon + 1);
    }
    s.remove(QLatin1Char('Z'));
    if (s.size() >= 8) {
        const int y = s.mid(0, 4).toInt();
        const int mo = s.mid(4, 2).toInt();
        const int d = s.mid(6, 2).toInt();
        int h = 0, mi = 0, sec = 0;
        if (s.size() >= 15 && s[8] == QLatin1Char('T')) {
            h = s.mid(9, 2).toInt();
            mi = s.mid(11, 2).toInt();
            sec = s.mid(13, 2).toInt();
        }
        QDateTime dt(QDate(y, mo, d), QTime(h, mi, sec), Qt::UTC);
        if (dt.isValid()) return dt.toSecsSinceEpoch();
    }
    return 0;
}

std::vector<CalEvent> parseIcsFile(const QString& path) {
    std::vector<CalEvent> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream in(&f);
    CalEvent cur;
    bool inEvent = false;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        // Unfold: continuation lines start with space — rare in simple exports.
        if (line.startsWith(QLatin1String("BEGIN:VEVENT"))) {
            inEvent = true;
            cur = {};
            continue;
        }
        if (line.startsWith(QLatin1String("END:VEVENT"))) {
            if (inEvent && (!cur.summary.empty() || cur.startUnix != 0))
                out.push_back(cur);
            inEvent = false;
            continue;
        }
        if (!inEvent) continue;
        if (line.startsWith(QLatin1String("SUMMARY"))) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon >= 0)
                cur.summary = line.mid(colon + 1).toStdString();
        } else if (line.startsWith(QLatin1String("DTSTART"))) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon >= 0) {
                cur.start = line.mid(colon + 1).toStdString();
                cur.startUnix = parseIcsDate(line.mid(colon + 1));
            }
        } else if (line.startsWith(QLatin1String("DTEND"))) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon >= 0) cur.end = line.mid(colon + 1).toStdString();
        }
    }
    return out;
}

std::vector<std::string> splitSemi(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(';', i);
        if (j == std::string::npos) j = s.size();
        std::string e = s.substr(i, j - i);
        while (!e.empty() && (e.front() == ' ' || e.front() == '\t')) e.erase(e.begin());
        while (!e.empty() && (e.back() == ' ' || e.back() == '\t')) e.pop_back();
        if (!e.empty()) out.push_back(e);
        i = j + 1;
    }
    return out;
}

} // namespace

std::string CalendarReadTool::name() const { return "calendar_read"; }
std::string CalendarReadTool::description() const {
    return "Read upcoming events from local .ics calendar files configured in "
           "advisor.calendar_paths (or pass paths[]). No cloud OAuth. "
           "Example: {\"hours\":24}";
}

nlohmann::json CalendarReadTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"hours", {{"type", "integer"},
                       {"description", "Lookahead window in hours (default 24)"}}},
            {"paths", {{"type", "array"},
                       {"items", {{"type", "string"}}},
                       {"description", "Optional .ics paths; else settings"}}},
        }},
    };
}

ToolResult CalendarReadTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    int hours = args.value("hours", 24);
    if (hours < 1) hours = 1;
    if (hours > 24 * 30) hours = 24 * 30;

    std::vector<std::string> paths;
    if (args.contains("paths") && args["paths"].is_array()) {
        for (const auto& p : args["paths"])
            if (p.is_string()) paths.push_back(p.get<std::string>());
    }
    if (paths.empty() && ctx.db) {
        paths = splitSemi(ctx.db->getSetting(keys::AdvisorCalendarPaths, ""));
    }
    if (paths.empty()) {
        return {false,
                {{"error", "no calendar paths — set advisor.calendar_paths or pass paths[]"}},
                "calendar_read: no paths"};
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 until = now + static_cast<qint64>(hours) * 3600;

    nlohmann::json events = nlohmann::json::array();
    for (const auto& p : paths) {
        const QString qp = QString::fromStdString(p);
        if (!QFileInfo::exists(qp)) continue;
        for (const auto& ev : parseIcsFile(qp)) {
            if (ev.startUnix != 0 && (ev.startUnix < now - 3600 || ev.startUnix > until))
                continue;
            events.push_back({
                {"summary", ev.summary},
                {"start", ev.start},
                {"end", ev.end},
                {"start_unix", ev.startUnix},
                {"source", p},
            });
        }
    }
    std::sort(events.begin(), events.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.value("start_unix", 0) < b.value("start_unix", 0);
    });

    PM_INFO("calendar_read: {} events from {} path(s)", events.size(), paths.size());
    return {true,
            {{"count", events.size()}, {"hours", hours}, {"events", events}},
            "calendar_read: " + std::to_string(events.size()) + " events"};
}

std::string InboxNotesTool::name() const { return "inbox_notes"; }
std::string InboxNotesTool::description() const {
    return "List notes from a local drop folder (advisor.inbox_dir) of .txt/.eml files. "
           "No IMAP/OAuth. Example: {\"limit\":10}";
}

nlohmann::json InboxNotesTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"dir", {{"type", "string"}, {"description", "Override drop folder"}}},
            {"limit", {{"type", "integer"}, {"description", "Max files (default 20)"}}},
        }},
    };
}

ToolResult InboxNotesTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    std::string dir = args.value("dir", "");
    if (dir.empty() && ctx.db)
        dir = ctx.db->getSetting(keys::AdvisorInboxDir, "");
    if (dir.empty()) {
        // Default beside app data
        try {
            dir = (Paths::instance().root() / "inbox").string();
        } catch (...) {}
    }
    if (dir.empty())
        return {false, {{"error", "no inbox dir"}}, "inbox_notes: no dir"};

    int limit = args.value("limit", 20);
    if (limit < 1) limit = 1;
    if (limit > 100) limit = 100;

    QDir qdir(QString::fromStdString(dir));
    if (!qdir.exists()) {
        return {true,
                {{"dir", dir}, {"count", 0}, {"notes", nlohmann::json::array()},
                 {"hint", "create the folder and drop .txt/.eml files"}},
                "inbox_notes: empty (dir missing)"};
    }

    const QStringList files = qdir.entryList(
        QStringList() << QStringLiteral("*.txt") << QStringLiteral("*.eml")
                      << QStringLiteral("*.md"),
        QDir::Files, QDir::Time);

    nlohmann::json notes = nlohmann::json::array();
    int n = 0;
    for (const QString& name : files) {
        if (n >= limit) break;
        const QString path = qdir.absoluteFilePath(name);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QByteArray body = f.read(4096);
        f.close();
        QString text = QString::fromUtf8(body);
        // Crude subject for .eml
        QString subject = name;
        if (name.endsWith(QLatin1String(".eml"), Qt::CaseInsensitive)) {
            for (const QString& line : text.split(QLatin1Char('\n'))) {
                if (line.startsWith(QLatin1String("Subject:"), Qt::CaseInsensitive)) {
                    subject = line.mid(8).trimmed();
                    break;
                }
            }
        }
        notes.push_back({
            {"file", name.toStdString()},
            {"path", path.toStdString()},
            {"subject", subject.toStdString()},
            {"snippet", text.left(400).toStdString()},
        });
        ++n;
    }

    return {true,
            {{"dir", dir}, {"count", notes.size()}, {"notes", notes}},
            "inbox_notes: " + std::to_string(notes.size()) + " files"};
}

} // namespace polymath
