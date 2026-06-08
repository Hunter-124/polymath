#pragma once
//
// tool_support — small, dependency-light helpers shared by the agent's builtin
// tools. Internal to src/agent (NOT part of the frozen public contract).
//
// Covers:
//   * httpGet / httpPost — synchronous HTTP using Qt Network, safe to call from
//     the agent worker thread (each call spins a local QEventLoop; no UI-thread
//     event loop is required). Tools run on the agent's QThread, so blocking
//     here never stalls the UI.
//   * htmlToText — a tiny readability-style extractor (strip script/style/markup,
//     collapse whitespace) used by fetch_page and web_search snippet cleanup.
//   * slugify / nowUnix — filename + timestamp helpers used by the doc tools.
//
#include <nlohmann/json.hpp>
#include <QByteArray>
#include <QString>
#include <cstdint>
#include <string>

namespace polymath::tool_support {

// Result of a blocking HTTP request.
struct HttpResponse {
    bool        ok = false;        // transport-level success (no network error)
    int         status = 0;        // HTTP status code (0 if the request never completed)
    QByteArray  body;              // raw response bytes
    QString     contentType;       // value of the Content-Type header (lowercased)
    QString     error;             // human-readable error if !ok
    QString     finalUrl;          // URL after any redirects
};

// Blocking GET. `headers` are extra request headers (e.g. {"Accept","..."}).
// `timeout_ms` aborts a stalled request. Redirects are followed (same-origin and
// cross-origin, capped by Qt's redirect policy).
HttpResponse httpGet(const QString& url,
                     const nlohmann::json& headers = nlohmann::json::object(),
                     int timeout_ms = 20000);

// Blocking POST with an explicit content type (e.g. application/json).
HttpResponse httpPost(const QString& url, const QByteArray& body,
                      const QString& contentType,
                      const nlohmann::json& headers = nlohmann::json::object(),
                      int timeout_ms = 20000);

// Strip HTML to readable plain text: drops <script>/<style>/<head>, removes all
// remaining tags, decodes the common named/numeric entities, and collapses
// runs of whitespace. `max_chars` truncates the result (0 = no limit).
std::string htmlToText(const QByteArray& html, size_t max_chars = 0);

// Best-effort <title> extraction from an HTML document ("" if none).
std::string htmlTitle(const QByteArray& html);

// Lowercase, hyphen-separated, filesystem-safe slug of `s` (for file names).
std::string slugify(const std::string& s, size_t max_len = 64);

// Seconds since the unix epoch (wraps to_unix(Clock::now())).
int64_t nowUnix();

} // namespace polymath::tool_support
