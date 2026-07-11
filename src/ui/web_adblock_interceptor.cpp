#include "web_adblock_interceptor.h"
#include "paths.h"
#include "logging.h"

#include <QWebEngineUrlRequestInfo>
#include <QUrl>

#include <filesystem>
#include <fstream>
#include <string>

namespace polymath {

namespace {

namespace fs = std::filesystem;

// Common ad / tracker hosts (suffix match on host), plus 2026 YouTube
// ad/analytics hardening additions (B3).
const QSet<QString>& defaultBlockedHosts() {
    static const QSet<QString> hosts = {
        QStringLiteral("doubleclick.net"),
        QStringLiteral("googlesyndication.com"),
        QStringLiteral("googleadservices.com"),
        QStringLiteral("google-analytics.com"),
        QStringLiteral("googletagmanager.com"),
        QStringLiteral("googletagservices.com"),
        QStringLiteral("pagead2.googlesyndication.com"),
        QStringLiteral("adservice.google.com"),
        QStringLiteral("ads.youtube.com"),
        QStringLiteral("pagead.l.doubleclick.net"),
        QStringLiteral("static.doubleclick.net"),
        QStringLiteral("ad.doubleclick.net"),
        QStringLiteral("securepubads.g.doubleclick.net"),
        QStringLiteral("fundingchoicesmessages.google.com"),
        QStringLiteral("tpc.googlesyndication.com"),
        QStringLiteral("partnerad.l.doubleclick.net"),
        QStringLiteral("ad.youtube.com"),
        QStringLiteral("facebook.net"),
        QStringLiteral("facebook.com/tr"),
        QStringLiteral("connect.facebook.net"),
        QStringLiteral("scorecardresearch.com"),
        QStringLiteral("adsrvr.org"),
        QStringLiteral("adnxs.com"),
        QStringLiteral("rubiconproject.com"),
        QStringLiteral("taboola.com"),
        QStringLiteral("outbrain.com"),
        QStringLiteral("criteo.com"),
        QStringLiteral("amazon-adsystem.com"),
        QStringLiteral("moatads.com"),
        QStringLiteral("hotjar.com"),
        QStringLiteral("sentry.io"),
        // --- B3: 2026 YouTube ad/analytics hardening additions ---
        QStringLiteral("googleads.g.doubleclick.net"),
        QStringLiteral("imasdk.googleapis.com"),   // Google IMA SDK (video ad requests)
        QStringLiteral("2mdn.net"),                // DoubleClick creative CDN
    };
    return hosts;
}

// Wildcard-TLD host prefixes: blocked if host.startsWith(prefix). Covers
// "adservice.google.*" (adservice.google.com / .de / .co.uk / ...) which a
// plain suffix match on "adservice.google.com" alone would miss.
const QSet<QString>& defaultBlockedHostPrefixes() {
    static const QSet<QString> prefixes = {
        QStringLiteral("adservice.google."),
    };
    return prefixes;
}

// Path/query markers on otherwise-allowed hosts (e.g. youtube.com). Matching
// against the full URL string means "youtube.com/pagead/" and
// "youtube.com/ptracking" from the B3 spec fall out of the existing
// "/pagead/" and "/ptracking" markers below (host already gates this to
// youtube.com/googlevideo.com/ytimg.com — see classify()).
const QSet<QString>& defaultBlockedPathMarkers() {
    static const QSet<QString> markers = {
        QStringLiteral("/pagead/"),
        QStringLiteral("/ptracking"),
        QStringLiteral("get_video_info"),
        QStringLiteral("/api/stats/ads"),
        QStringLiteral("/pagead/adview"),
        QStringLiteral("/pagead/conversion"),
        QStringLiteral("/pcs/activeview"),
        QStringLiteral("ad_type="),
        QStringLiteral("oad="),
        QStringLiteral("/youtubei/v1/log_event"),
    };
    return markers;
}

bool hostMatches(const QString& host, const QSet<QString>& hosts,
                  const QSet<QString>& prefixes) {
    for (const QString& blocked : hosts) {
        if (host == blocked || host.endsWith(QLatin1Char('.') + blocked))
            return true;
    }
    for (const QString& prefix : prefixes) {
        if (host.startsWith(prefix))
            return true;
    }
    return false;
}

bool classify(const QUrl& url, const QSet<QString>& hosts,
              const QSet<QString>& prefixes, const QSet<QString>& markers) {
    const QString host = url.host().toLower();
    if (host.isEmpty())
        return false;

    if (hostMatches(host, hosts, prefixes))
        return true;

    // YouTube-specific ad paths / stream heuristics.
    if (host.contains(QLatin1String("youtube.com")) ||
        host.contains(QLatin1String("googlevideo.com")) ||
        host.contains(QLatin1String("ytimg.com"))) {
        const QString full = url.toString(QUrl::RemoveUserInfo);
        for (const QString& marker : markers) {
            if (full.contains(marker, Qt::CaseInsensitive))
                return true;
        }
        // googlevideo ad streams often carry &oad= or &ctier=L for ads.
        if (host.contains(QLatin1String("googlevideo.com"))) {
            if (url.query().contains(QLatin1String("oad=")) ||
                url.query().contains(QLatin1String("ctier=L")))
                return true;
        }
    }
    return false;
}

// Trim ASCII whitespace from both ends (host lines from a plain-text file).
std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

bool isAdRequest(const QString& url) {
    return classify(QUrl(url), defaultBlockedHosts(), defaultBlockedHostPrefixes(),
                     defaultBlockedPathMarkers());
}

WebAdblockInterceptor::WebAdblockInterceptor(QObject* parent)
    : QWebEngineUrlRequestInterceptor(parent) {
    blocked_hosts_ = defaultBlockedHosts();
    blocked_host_prefixes_ = defaultBlockedHostPrefixes();
    blocked_path_markers_ = defaultBlockedPathMarkers();
    loadExtraHostsFile();
}

void WebAdblockInterceptor::loadExtraHostsFile() {
    // data/adblock_extra.txt in the deployed/portable layout == Paths root
    // (a writable `data/` folder beside the exe, or %LOCALAPPDATA%/Polymath).
    // Paths::instance().setRoot() runs before this constructor in main.cpp.
    const fs::path root = Paths::instance().root();
    if (root.empty())
        return;   // not yet configured (e.g. some test harnesses) — tolerate.

    const fs::path extraFile = root / "adblock_extra.txt";
    std::error_code ec;
    if (!fs::exists(extraFile, ec) || ec)
        return;   // optional file absent — not an error.

    std::ifstream in(extraFile);
    if (!in.is_open())
        return;

    std::string line;
    int loaded = 0;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#')
            continue;
        const QString host = QString::fromStdString(t).toLower();
        if (host.isEmpty())
            continue;
        blocked_hosts_.insert(host);
        ++loaded;
    }
    extra_hosts_file_count_ = loaded;
    if (loaded > 0)
        PM_INFO("adblock: loaded {} extra host(s) from {}", loaded, extraFile.string());
}

void WebAdblockInterceptor::addBlockedHost(const QString& host) {
    blocked_hosts_.insert(host.toLower());
}

bool WebAdblockInterceptor::shouldBlock(const QUrl& url) const {
    return classify(url, blocked_hosts_, blocked_host_prefixes_, blocked_path_markers_);
}

void WebAdblockInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info) {
    if (shouldBlock(info.requestUrl()))
        info.block(true);
}

} // namespace polymath
