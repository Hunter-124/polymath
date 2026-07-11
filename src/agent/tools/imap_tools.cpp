#include "imap_tools.h"
#include "config.h"
#include "database.h"
#include "logging.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QSslSocket>
#include <QString>
#include <QStringList>

#include <string>
#include <vector>

namespace polymath {

namespace {

// Minimal IMAP over SSL (port 993): LOGIN, SELECT INBOX, FETCH latest N ENVELOPE/BODY.PEEK.
// Uses app passwords; never logs credentials. Safety: External risk class.

bool waitReady(QSslSocket& sock, int ms = 15000) {
    return sock.waitForReadyRead(ms) || sock.bytesAvailable() > 0;
}

QByteArray readLine(QSslSocket& sock) {
    while (!sock.canReadLine()) {
        if (!sock.waitForReadyRead(10000)) return {};
    }
    return sock.readLine();
}

QByteArray readUntilTagged(QSslSocket& sock, const QByteArray& tag) {
    QByteArray all;
    for (;;) {
        QByteArray line = readLine(sock);
        if (line.isEmpty()) break;
        all += line;
        if (line.startsWith(tag + " OK") || line.startsWith(tag + " NO")
            || line.startsWith(tag + " BAD"))
            break;
    }
    return all;
}

} // namespace

std::string ImapFetchTool::name() const { return "email_fetch"; }
std::string ImapFetchTool::description() const {
    return "Fetch recent email subjects via IMAP SSL (settings advisor.imap_host, "
           "advisor.imap_user, advisor.imap_pass — app password). "
           "Example: {\"limit\":5}";
}

nlohmann::json ImapFetchTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"limit", {{"type", "integer"}, {"description", "Max messages (default 5)"}}},
            {"host", {{"type", "string"}}},
            {"user", {{"type", "string"}}},
            {"pass", {{"type", "string"}, {"description", "Prefer app password; not logged"}}},
            {"port", {{"type", "integer"}, {"description", "Default 993"}}},
        }},
    };
}

ToolResult ImapFetchTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    int limit = args.value("limit", 5);
    if (limit < 1) limit = 1;
    if (limit > 25) limit = 25;

    std::string host = args.value("host", "");
    std::string user = args.value("user", "");
    std::string pass = args.value("pass", "");
    int port = args.value("port", 993);
    if (ctx.db) {
        if (host.empty()) host = ctx.db->getSetting("advisor.imap_host", "");
        if (user.empty()) user = ctx.db->getSetting("advisor.imap_user", "");
        if (pass.empty()) pass = ctx.db->getSetting("advisor.imap_pass", "");
        if (port <= 0) {
            try { port = std::stoi(ctx.db->getSetting("advisor.imap_port", "993")); }
            catch (...) { port = 993; }
        }
    }
    if (host.empty() || user.empty() || pass.empty()) {
        return {false,
                {{"error", "configure advisor.imap_host/user/pass (app password) or pass args"},
                 {"hint", "also use inbox_notes for a local drop folder without IMAP"}},
                "email_fetch: missing credentials"};
    }

    QSslSocket sock;
    sock.setPeerVerifyMode(QSslSocket::QueryPeer);  // many home servers use custom CA
    sock.connectToHostEncrypted(QString::fromStdString(host), static_cast<quint16>(port));
    if (!sock.waitForEncrypted(15000)) {
        return {false,
                {{"error", "TLS connect failed"}, {"detail", sock.errorString().toStdString()}},
                "email_fetch: connect failed"};
    }
    // greeting
    readLine(sock);

    auto cmd = [&](const char* tag, const QString& line) -> QByteArray {
        sock.write(QByteArray(tag) + " " + line.toUtf8() + "\r\n");
        sock.flush();
        return readUntilTagged(sock, QByteArray(tag));
    };

    // LOGIN — quote user/pass simply (escape quotes)
    auto q = [](const std::string& s) {
        QString o = QString::fromStdString(s);
        o.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        o.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        return QStringLiteral("\"%1\"").arg(o);
    };
    QByteArray login = cmd("a01", QStringLiteral("LOGIN %1 %2").arg(q(user), q(pass)));
    if (!login.contains("a01 OK")) {
        PM_WARN("email_fetch: LOGIN rejected");
        return {false, {{"error", "LOGIN failed (check app password)"}}, "email_fetch: login failed"};
    }
    cmd("a02", QStringLiteral("SELECT INBOX"));
    // SEARCH recent
    QByteArray search = cmd("a03", QStringLiteral("SEARCH ALL"));
    // Parse sequence numbers from "* SEARCH 1 2 3"
    std::vector<int> ids;
    for (const QByteArray& line : search.split('\n')) {
        if (!line.startsWith("* SEARCH")) continue;
        const auto parts = QString::fromUtf8(line).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (int i = 2; i < parts.size(); ++i) {
            bool ok = false;
            int n = parts[i].toInt(&ok);
            if (ok) ids.push_back(n);
        }
    }
    if (ids.empty()) {
        cmd("a99", QStringLiteral("LOGOUT"));
        return {true, {{"count", 0}, {"messages", nlohmann::json::array()}}, "email_fetch: empty inbox"};
    }
    // Take last `limit` ids
    if (static_cast<int>(ids.size()) > limit)
        ids.erase(ids.begin(), ids.end() - limit);

    nlohmann::json messages = nlohmann::json::array();
    int tagN = 10;
    for (int id : ids) {
        const QString tag = QStringLiteral("a%1").arg(tagN++);
        QByteArray body = cmd(tag.toUtf8().constData(),
                              QStringLiteral("FETCH %1 (ENVELOPE BODY.PEEK[HEADER.FIELDS (SUBJECT FROM DATE)])")
                                  .arg(id));
        QString subject, from, date;
        const QString text = QString::fromUtf8(body);
        // Crude header parse
        for (const QString& line : text.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("Subject:"), Qt::CaseInsensitive))
                subject = line.mid(8).trimmed();
            else if (line.startsWith(QLatin1String("From:"), Qt::CaseInsensitive))
                from = line.mid(5).trimmed();
            else if (line.startsWith(QLatin1String("Date:"), Qt::CaseInsensitive))
                date = line.mid(5).trimmed();
        }
        messages.push_back({
            {"id", id},
            {"subject", subject.toStdString()},
            {"from", from.toStdString()},
            {"date", date.toStdString()},
        });
    }
    cmd("a99", QStringLiteral("LOGOUT"));
    PM_INFO("email_fetch: {} message(s) from {}", messages.size(), host);
    return {true,
            {{"count", messages.size()}, {"host", host}, {"messages", messages}},
            "email_fetch: " + std::to_string(messages.size()) + " messages"};
}

} // namespace polymath
