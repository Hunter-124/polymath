#pragma once
//
// WebAdblockInterceptor — QWebEngineUrlRequestInterceptor that blocks common
// ad / tracker hosts and YouTube ad endpoints (D5).
//
#include <QWebEngineUrlRequestInterceptor>
#include <QSet>
#include <QString>

namespace polymath {

class WebAdblockInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit WebAdblockInterceptor(QObject* parent = nullptr);

    void interceptRequest(QWebEngineUrlRequestInfo& info) override;

    // Extra host suffixes to block (lowercase, no scheme). Empty by default.
    Q_INVOKABLE void addBlockedHost(const QString& host);

private:
    bool shouldBlock(const QUrl& url) const;

    QSet<QString> blocked_hosts_;
    QSet<QString> blocked_path_markers_;
};

} // namespace polymath
