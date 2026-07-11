#pragma once
//
// WebAdblockInterceptor — QWebEngineUrlRequestInterceptor that blocks common
// ad / tracker hosts and YouTube ad endpoints (D5, hardened B3).
//
#include <QWebEngineUrlRequestInterceptor>
#include <QSet>
#include <QString>
#include <QUrl>

namespace polymath {

// Pure, state-free URL classifier (B3): true if `url` matches one of the
// interceptor's *built-in default* ad/tracker rules (hosts, wildcard-TLD
// host prefixes, YouTube ad path markers, and the googlevideo ctier=L/oad=
// stream heuristics). Does NOT see hosts added at runtime via
// addBlockedHost() or data/adblock_extra.txt — those are instance state on
// WebAdblockInterceptor. Exists so the classification rules are unit-testable
// without constructing a QWebEngine object (see tests/test_adblock.cpp).
bool isAdRequest(const QString& url);

class WebAdblockInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit WebAdblockInterceptor(QObject* parent = nullptr);

    void interceptRequest(QWebEngineUrlRequestInfo& info) override;

    // Extra host suffixes to block (lowercase, no scheme). Empty by default.
    Q_INVOKABLE void addBlockedHost(const QString& host);

    // How many hosts were loaded from the optional data/adblock_extra.txt at
    // construction time (0 if the file is absent/empty/unreadable).
    int extraHostsFileCount() const { return extra_hosts_file_count_; }

private:
    bool shouldBlock(const QUrl& url) const;
    // Loads Paths::instance().root()/"adblock_extra.txt" (i.e. data/adblock_extra.txt
    // in the deployed/portable layout) if present: one host suffix per line,
    // blank lines and lines starting with '#' ignored, hosts lower-cased.
    // Tolerates a missing file entirely (no-op, not an error).
    void loadExtraHostsFile();

    QSet<QString> blocked_hosts_;
    // Wildcard-TLD host prefixes, matched via QString::startsWith(prefix) —
    // e.g. "adservice.google." blocks adservice.google.com, .de, .co.uk, ...
    QSet<QString> blocked_host_prefixes_;
    QSet<QString> blocked_path_markers_;
    int extra_hosts_file_count_ = 0;
};

} // namespace polymath
