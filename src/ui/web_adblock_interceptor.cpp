#include "web_adblock_interceptor.h"

#include <QWebEngineUrlRequestInfo>
#include <QUrl>

namespace polymath {

WebAdblockInterceptor::WebAdblockInterceptor(QObject* parent)
    : QWebEngineUrlRequestInterceptor(parent) {
    // Common ad / tracker hosts (suffix match on host).
    const char* hosts[] = {
        "doubleclick.net",
        "googlesyndication.com",
        "googleadservices.com",
        "google-analytics.com",
        "googletagmanager.com",
        "googletagservices.com",
        "pagead2.googlesyndication.com",
        "adservice.google.com",
        "ads.youtube.com",
        "pagead.l.doubleclick.net",
        "static.doubleclick.net",
        "ad.doubleclick.net",
        "securepubads.g.doubleclick.net",
        "fundingchoicesmessages.google.com",
        "tpc.googlesyndication.com",
        "partnerad.l.doubleclick.net",
        "ad.youtube.com",
        "facebook.net",
        "facebook.com/tr",
        "connect.facebook.net",
        "scorecardresearch.com",
        "adsrvr.org",
        "adnxs.com",
        "rubiconproject.com",
        "taboola.com",
        "outbrain.com",
        "criteo.com",
        "amazon-adsystem.com",
        "moatads.com",
        "hotjar.com",
        "sentry.io",
    };
    for (const char* h : hosts)
        blocked_hosts_.insert(QString::fromLatin1(h));

    // Path/query markers on otherwise-allowed hosts (e.g. youtube.com).
    const char* paths[] = {
        "/pagead/",
        "/ptracking",
        "get_video_info",
        "/api/stats/ads",
        "/pagead/adview",
        "/pagead/conversion",
        "/pcs/activeview",
        "ad_type=",
        "oad=",
        "/youtubei/v1/log_event",
    };
    for (const char* p : paths)
        blocked_path_markers_.insert(QString::fromLatin1(p));
}

void WebAdblockInterceptor::addBlockedHost(const QString& host) {
    blocked_hosts_.insert(host.toLower());
}

bool WebAdblockInterceptor::shouldBlock(const QUrl& url) const {
    const QString host = url.host().toLower();
    if (host.isEmpty())
        return false;

    for (const QString& blocked : blocked_hosts_) {
        if (host == blocked || host.endsWith(QLatin1Char('.') + blocked))
            return true;
    }

    // YouTube-specific ad paths
    if (host.contains(QLatin1String("youtube.com")) ||
        host.contains(QLatin1String("googlevideo.com")) ||
        host.contains(QLatin1String("ytimg.com"))) {
        const QString full = url.toString(QUrl::RemoveUserInfo);
        for (const QString& marker : blocked_path_markers_) {
            if (full.contains(marker, Qt::CaseInsensitive))
                return true;
        }
        // googlevideo ad streams often carry &oad= or ctag= for ads
        if (host.contains(QLatin1String("googlevideo.com"))) {
            if (url.query().contains(QLatin1String("oad=")) ||
                url.query().contains(QLatin1String("ctier=L")))
                return true;
        }
    }
    return false;
}

void WebAdblockInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info) {
    if (shouldBlock(info.requestUrl()))
        info.block(true);
}

} // namespace polymath
