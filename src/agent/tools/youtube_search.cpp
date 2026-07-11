#include "youtube_search.h"
#include "tool_support.h"
#include "logging.h"

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <cctype>
#include <set>
#include <string>
#include <vector>

// youtube_search — no API key required. Primary: POST the public Innertube
// endpoint (the same one youtube.com's own web client calls) with the
// standard "WEB" client context and a plain query. Fallback: GET the
// classic results page and scrape the `ytInitialData` JSON blob embedded in
// a <script> tag. Both responses share the same shape (a tree containing
// `videoRenderer` nodes for each video card), so one recursive walker
// handles both — see youtube_search_internal::extractVideosFromJson.

namespace polymath {

namespace {

using tool_support::HttpResponse;

// --- small JSON text helpers -------------------------------------------------

// YouTube renders label text as either {"simpleText": "..."} or
// {"runs":[{"text":"..."},...]} (multiple runs for mixed styling); concatenate
// runs so nothing is lost.
std::string textFrom(const nlohmann::json& node) {
    if (!node.is_object()) return {};
    if (node.contains("simpleText") && node["simpleText"].is_string())
        return node["simpleText"].get<std::string>();
    if (node.contains("runs") && node["runs"].is_array()) {
        std::string out;
        for (const auto& r : node["runs"]) out += r.value("text", "");
        return out;
    }
    return {};
}

// Largest available thumbnail (thumbnails[] is ordered small -> large).
std::string thumbnailFrom(const nlohmann::json& vr) {
    if (!vr.contains("thumbnail")) return {};
    const auto& thumb = vr["thumbnail"];
    if (!thumb.contains("thumbnails") || !thumb["thumbnails"].is_array()) return {};
    const auto& thumbs = thumb["thumbnails"];
    if (thumbs.empty()) return {};
    return thumbs.back().value("url", "");
}

nlohmann::json extractVideo(const nlohmann::json& vr) {
    const std::string videoId = vr.value("videoId", "");

    std::string channel = textFrom(vr.value("ownerText", nlohmann::json::object()));
    if (channel.empty()) channel = textFrom(vr.value("longBylineText", nlohmann::json::object()));
    if (channel.empty()) channel = textFrom(vr.value("shortBylineText", nlohmann::json::object()));

    std::string views = textFrom(vr.value("shortViewCountText", nlohmann::json::object()));
    if (views.empty()) views = textFrom(vr.value("viewCountText", nlohmann::json::object()));

    const std::string durationText = textFrom(vr.value("lengthText", nlohmann::json::object()));

    return {
        {"videoId", videoId},
        {"title", textFrom(vr.value("title", nlohmann::json::object()))},
        {"channel", channel},
        {"durationSec", youtube_search_internal::parseDurationSeconds(durationText)},
        {"views", views},
        {"publishedText", textFrom(vr.value("publishedTimeText", nlohmann::json::object()))},
        {"thumbnailUrl", thumbnailFrom(vr)},
        {"watchUrl", videoId.empty() ? "" : ("https://www.youtube.com/watch?v=" + videoId)},
    };
}

// Recursively hunts for `videoRenderer` keys anywhere in the tree. Innertube's
// search response nests them under
// contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer...
// and ytInitialData nests them slightly differently again; walking instead of
// hardcoding a path keeps this resilient to YouTube's frequent internal
// reshuffles.
void collectVideoRenderers(const nlohmann::json& node, nlohmann::json& out,
                           std::set<std::string>& seen, int max_results) {
    if (static_cast<int>(out.size()) >= max_results) return;

    if (node.is_object()) {
        auto it = node.find("videoRenderer");
        if (it != node.end() && it->is_object()) {
            const std::string vid = it->value("videoId", "");
            if (!vid.empty() && seen.insert(vid).second)
                out.push_back(extractVideo(*it));
        }
        for (auto it2 = node.begin(); it2 != node.end(); ++it2) {
            if (static_cast<int>(out.size()) >= max_results) return;
            if (it2.key() == "videoRenderer") continue;   // already handled above
            collectVideoRenderers(it2.value(), out, seen, max_results);
        }
    } else if (node.is_array()) {
        for (const auto& v : node) {
            if (static_cast<int>(out.size()) >= max_results) return;
            collectVideoRenderers(v, out, seen, max_results);
        }
    }
}

// Finds the matching closing brace for the '{' at `open`, treating quoted
// string contents (with backslash escapes) as opaque so stray '}'/'{' inside
// JS string literals elsewhere on the page can't confuse it. Returns "" if
// unbalanced.
std::string extractBalancedJson(const std::string& s, size_t open) {
    if (open >= s.size() || s[open] != '{') return {};
    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t i = open; i < s.size(); ++i) {
        const char c = s[i];
        if (inString) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) return s.substr(open, i - open + 1);
        }
    }
    return {};
}

// Best-effort, widely-referenced (if occasionally drifting) unofficial
// YouTube search "sort" protobuf-base64 params. Only affects request
// construction (not covered by the fixture test); an unrecognized/empty
// order is simply omitted, leaving YouTube's default relevance ranking.
std::string searchSortParams(const std::string& order) {
    if (order == "date" || order == "upload_date") return "CAI=";
    if (order == "views" || order == "view_count")  return "CAM=";
    if (order == "rating")                          return "CAE=";
    return {};
}

} // namespace

namespace youtube_search_internal {

int parseDurationSeconds(const std::string& text) {
    std::vector<int> parts;
    std::string cur;
    bool any = false;
    for (char c : text) {
        if (c == ':') {
            parts.push_back(cur.empty() ? 0 : std::atoi(cur.c_str()));
            cur.clear();
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            cur += c;
            any = true;
        }
        // Non-digit, non-colon chars (e.g. accessibility text) are ignored.
    }
    if (!cur.empty()) parts.push_back(std::atoi(cur.c_str()));
    if (!any || parts.empty()) return 0;

    int secs = 0;
    for (int p : parts) secs = secs * 60 + p;
    return secs;
}

nlohmann::json extractVideosFromJson(const nlohmann::json& doc, int max_results) {
    nlohmann::json out = nlohmann::json::array();
    if (max_results <= 0) return out;
    std::set<std::string> seen;
    collectVideoRenderers(doc, out, seen, max_results);
    return out;
}

std::string extractYtInitialDataJson(const std::string& html) {
    size_t pos = html.find("var ytInitialData");
    if (pos == std::string::npos) pos = html.find("\"ytInitialData\"");
    if (pos == std::string::npos) pos = html.find("ytInitialData");
    if (pos == std::string::npos) return {};

    const size_t eq = html.find('=', pos);
    if (eq == std::string::npos) return {};
    const size_t brace = html.find('{', eq);
    if (brace == std::string::npos) return {};
    return extractBalancedJson(html, brace);
}

} // namespace youtube_search_internal

std::string YoutubeSearchTool::name() const { return "youtube_search"; }
std::string YoutubeSearchTool::description() const {
    return "Search YouTube (no account/key needed) and return a short list of videos: "
           "title, channel, duration, views, thumbnail, and a watchUrl. Use with "
           "ui_control spawn_surface to show a video picker or play a result.";
}

nlohmann::json YoutubeSearchTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"query",       {{"type", "string"},  {"description", "What to search YouTube for"}}},
            {"max_results", {{"type", "integer"}, {"description", "Max videos to return (default 6)"}}},
            {"order",       {{"type", "string"},  {"description",
                "Optional sort: \"relevance\" (default), \"date\", \"views\", or \"rating\""}}},
        }},
        {"required", {"query"}},
    };
}

ToolResult YoutubeSearchTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string query = args.value("query", "");
    int max_results = args.value("max_results", 6);
    if (max_results <= 0 || max_results > 20) max_results = 6;
    const std::string order = args.value("order", "");

    if (query.empty())
        return {false, {{"error", "query required"}}, "youtube_search: missing query"};

    const std::string sp = searchSortParams(order);
    bool anyTransportOk = false;
    nlohmann::json results = nlohmann::json::array();

    // --- Primary: Innertube web-client search endpoint (no key) -------------
    {
        const std::string clientVersion =
            "2." + QDate::currentDate().toString("yyyyMMdd").toStdString() + ".00.00";
        nlohmann::json body = {
            {"context", {{"client", {{"clientName", "WEB"}, {"clientVersion", clientVersion}}}}},
            {"query", query},
        };
        if (!sp.empty()) body["params"] = sp;

        nlohmann::json headers = {
            {"Accept", "application/json"},
            {"X-YouTube-Client-Name", "1"},
            {"X-YouTube-Client-Version", clientVersion},
        };
        HttpResponse resp = tool_support::httpPost(
            QStringLiteral("https://www.youtube.com/youtubei/v1/search"),
            QByteArray::fromStdString(body.dump()),
            QStringLiteral("application/json"), headers, /*timeout_ms=*/5000);

        if (resp.ok) {
            anyTransportOk = true;
            nlohmann::json doc = nlohmann::json::parse(resp.body.toStdString(), nullptr, false);
            if (!doc.is_discarded())
                results = youtube_search_internal::extractVideosFromJson(doc, max_results);
        } else {
            PM_WARN("youtube_search(innertube): {}", resp.error.toStdString());
        }
    }

    // --- Fallback: scrape ytInitialData off the classic results page --------
    if (results.empty()) {
        QUrl url(QStringLiteral("https://www.youtube.com/results"));
        QUrlQuery q;
        q.addQueryItem("search_query", QString::fromStdString(query));
        if (!sp.empty()) q.addQueryItem("sp", QString::fromStdString(sp));
        url.setQuery(q);

        nlohmann::json headers = {{"Accept", "text/html,application/xhtml+xml"}};
        HttpResponse resp = tool_support::httpGet(url.toString(), headers, /*timeout_ms=*/5000);

        if (resp.ok) {
            anyTransportOk = true;
            const std::string blob =
                youtube_search_internal::extractYtInitialDataJson(resp.body.toStdString());
            if (!blob.empty()) {
                nlohmann::json doc = nlohmann::json::parse(blob, nullptr, false);
                if (!doc.is_discarded())
                    results = youtube_search_internal::extractVideosFromJson(doc, max_results);
            }
        } else {
            PM_WARN("youtube_search(results): {}", resp.error.toStdString());
        }
    }

    if (!anyTransportOk)
        return {false, {{"error", "network unreachable"}, {"query", query}},
                "youtube unreachable"};

    const size_t n = results.size();
    nlohmann::json content = {{"query", query}, {"results", results}};
    if (n == 0)
        return {true, std::move(content), "youtube_search: no results for \"" + query + "\""};
    return {true, std::move(content),
            "Found " + std::to_string(n) + " video(s) for \"" + query + "\""};
}

} // namespace polymath
