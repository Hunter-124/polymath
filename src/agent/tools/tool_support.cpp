#include "tool_support.h"
#include "types.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cctype>

// Pinned against Qt 6.5+ (QNetworkRequest::NoLessSafeRedirectPolicy is the
// default since Qt 6; RedirectPolicyAttribute is stable across the 6.x series).

namespace polymath::tool_support {

namespace {

// Run a single QNetworkReply to completion on the calling thread, with a hard
// timeout. Returns a populated HttpResponse. The reply is deleteLater()'d.
HttpResponse awaitReply(QNetworkReply* reply, int timeout_ms) {
    HttpResponse out;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false;

    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timer.start(timeout_ms);
    loop.exec();
    timer.stop();

    out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    out.finalUrl = reply->url().toString();
    out.contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();

    if (timedOut) {
        out.ok = false;
        out.error = QStringLiteral("request timed out after %1 ms").arg(timeout_ms);
    } else if (reply->error() != QNetworkReply::NoError) {
        out.ok = false;
        out.error = reply->errorString();
        out.body = reply->readAll();   // some servers send a body with the error
    } else {
        out.ok = true;
        out.body = reply->readAll();
    }
    reply->deleteLater();
    return out;
}

void applyHeaders(QNetworkRequest& req, const nlohmann::json& headers) {
    // A sane default UA — many search/readability endpoints reject empty UAs.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Hearth/1.0 (+local assistant)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!headers.is_object()) return;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        if (!it.value().is_string()) continue;
        req.setRawHeader(QByteArray::fromStdString(it.key()),
                         QByteArray::fromStdString(it.value().get<std::string>()));
    }
}

} // namespace

HttpResponse httpGet(const QString& url, const nlohmann::json& headers, int timeout_ms) {
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(url)};
    applyHeaders(req, headers);
    return awaitReply(nam.get(req), timeout_ms);
}

HttpResponse httpPost(const QString& url, const QByteArray& body,
                      const QString& contentType, const nlohmann::json& headers,
                      int timeout_ms) {
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(url)};
    applyHeaders(req, headers);
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    return awaitReply(nam.post(req, body), timeout_ms);
}

// --- HTML -> text -----------------------------------------------------------

namespace {

QString decodeEntities(QString s) {
    // Decode the handful of entities that actually matter for readable text.
    static const std::pair<const char*, const char*> kNamed[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"&apos;", "'"}, {"&nbsp;", " "}, {"&mdash;", "—"},
        {"&ndash;", "–"}, {"&hellip;", "…"}, {"&rsquo;", "’"}, {"&lsquo;", "‘"},
        {"&ldquo;", "“"}, {"&rdquo;", "”"},
    };
    for (const auto& [from, to] : kNamed)
        s.replace(QLatin1String(from), QString::fromUtf8(to));   // `to` may be multi-byte UTF-8

    // Numeric entities (&#NNN; / &#xHHH;).
    static const QRegularExpression numRe(QStringLiteral("&#(x?)([0-9a-fA-F]+);"));
    QString out;
    out.reserve(s.size());
    int last = 0;
    auto it = numRe.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        const bool hex = !m.captured(1).isEmpty();
        bool ok = false;
        const uint cp = m.captured(2).toUInt(&ok, hex ? 16 : 10);
        if (ok && cp != 0) {
            if (cp <= 0xFFFF) out += QChar(static_cast<char16_t>(cp));
            else              out += QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
        }
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

} // namespace

std::string htmlToText(const QByteArray& html, size_t max_chars) {
    QString s = QString::fromUtf8(html);

    // Drop non-content regions wholesale (dot-matches-newline for spanning tags).
    static const QRegularExpression dropRe(
        QStringLiteral("<(script|style|head|noscript|svg|template)\\b[^>]*>.*?</\\1>"),
        QRegularExpression::CaseInsensitiveOption |
        QRegularExpression::DotMatchesEverythingOption);
    s.remove(dropRe);

    // Turn block-level boundaries into newlines so paragraphs survive.
    static const QRegularExpression brRe(
        QStringLiteral("<\\s*(br|/p|/div|/li|/h[1-6]|/tr)\\s*[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    s.replace(brRe, QStringLiteral("\n"));

    // Strip every remaining tag.
    static const QRegularExpression tagRe(QStringLiteral("<[^>]+>"));
    s.remove(tagRe);

    s = decodeEntities(s);

    // Collapse whitespace: trim each line, drop blank runs.
    static const QRegularExpression spaceRe(QStringLiteral("[ \\t\\x0B\\f\\r]+"));
    s.replace(spaceRe, QStringLiteral(" "));
    static const QRegularExpression blankRe(QStringLiteral("\\n\\s*\\n\\s*"));
    s.replace(blankRe, QStringLiteral("\n\n"));

    std::string out = s.trimmed().toStdString();
    if (max_chars && out.size() > max_chars) {
        out.resize(max_chars);
        out += "…";
    }
    return out;
}

std::string htmlTitle(const QByteArray& html) {
    static const QRegularExpression titleRe(
        QStringLiteral("<title\\b[^>]*>(.*?)</title>"),
        QRegularExpression::CaseInsensitiveOption |
        QRegularExpression::DotMatchesEverythingOption);
    const auto m = titleRe.match(QString::fromUtf8(html));
    if (!m.hasMatch()) return {};
    return decodeEntities(m.captured(1)).simplified().toStdString();
}

// --- misc -------------------------------------------------------------------

std::string slugify(const std::string& s, size_t max_len) {
    std::string out;
    out.reserve(std::min(s.size(), max_len));
    bool prevDash = false;
    for (char c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out += static_cast<char>(std::tolower(uc));
            prevDash = false;
        } else if (!prevDash && !out.empty()) {
            out += '-';
            prevDash = true;
        }
        if (out.size() >= max_len) break;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) out = "untitled";
    return out;
}

int64_t nowUnix() { return to_unix(Clock::now()); }

} // namespace polymath::tool_support
