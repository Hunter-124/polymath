#include "web_search.h"
#include "tool_support.h"
#include "database.h"
#include "config.h"
#include "logging.h"

#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

// web_search — query the configured search backend (SearXNG / Brave / DuckDuckGo)
// over Qt Network and return a small list of {title,url,snippet} results the
// model can reason over (and follow up with fetch_page).
//
// Backend is chosen by Config key web.search_backend (searxng|brave|ddg), with
// the API key/base in web.search_api_key. We keep parsing defensive: each
// backend returns different JSON, so a missing field degrades to "".

namespace polymath {

namespace {

using tool_support::HttpResponse;

// DuckDuckGo Instant Answer API (no key). Limited coverage (mostly topic/abstract
// + related topics) but a reasonable keyless default for a local assistant.
nlohmann::json searchDuckDuckGo(const std::string& query, int max_results) {
    QUrl url(QStringLiteral("https://api.duckduckgo.com/"));
    QUrlQuery q;
    q.addQueryItem("q", QString::fromStdString(query));
    q.addQueryItem("format", "json");
    q.addQueryItem("no_html", "1");
    q.addQueryItem("no_redirect", "1");
    url.setQuery(q);

    HttpResponse resp = tool_support::httpGet(url.toString());
    nlohmann::json results = nlohmann::json::array();
    if (!resp.ok) {
        PM_WARN("web_search(ddg): {}", resp.error.toStdString());
        return results;
    }

    nlohmann::json doc = nlohmann::json::parse(resp.body.toStdString(), nullptr, false);
    if (doc.is_discarded()) return results;

    // Primary abstract (if any).
    if (doc.value("AbstractText", "") != "") {
        results.push_back({
            {"title", doc.value("Heading", query)},
            {"url",   doc.value("AbstractURL", "")},
            {"snippet", doc.value("AbstractText", "")},
        });
    }
    // RelatedTopics may nest (Topics -> Topics). Flatten one level.
    std::function<void(const nlohmann::json&)> walk = [&](const nlohmann::json& arr) {
        if (!arr.is_array()) return;
        for (const auto& t : arr) {
            if (static_cast<int>(results.size()) >= max_results) return;
            if (t.contains("Topics")) { walk(t["Topics"]); continue; }
            if (!t.contains("Text")) continue;
            results.push_back({
                {"title", t.value("Text", "")},
                {"url",   t.value("FirstURL", "")},
                {"snippet", t.value("Text", "")},
            });
        }
    };
    walk(doc.value("RelatedTopics", nlohmann::json::array()));
    return results;
}

// SearXNG JSON API: GET <base>/search?q=...&format=json. `base` comes from the
// api-key setting (we overload it as the instance base URL for self-hosted).
nlohmann::json searchSearxng(const std::string& base, const std::string& query, int max_results) {
    std::string root = base.empty() ? "http://127.0.0.1:8888" : base;
    if (!root.empty() && root.back() == '/') root.pop_back();

    QUrl url(QString::fromStdString(root + "/search"));
    QUrlQuery q;
    q.addQueryItem("q", QString::fromStdString(query));
    q.addQueryItem("format", "json");
    url.setQuery(q);

    HttpResponse resp = tool_support::httpGet(url.toString());
    nlohmann::json results = nlohmann::json::array();
    if (!resp.ok) {
        PM_WARN("web_search(searxng): {}", resp.error.toStdString());
        return results;
    }
    nlohmann::json doc = nlohmann::json::parse(resp.body.toStdString(), nullptr, false);
    if (doc.is_discarded() || !doc.contains("results")) return results;

    for (const auto& r : doc["results"]) {
        if (static_cast<int>(results.size()) >= max_results) break;
        results.push_back({
            {"title",   r.value("title", "")},
            {"url",     r.value("url", "")},
            {"snippet", r.value("content", "")},
        });
    }
    return results;
}

// Brave Search API: GET https://api.search.brave.com/res/v1/web/search with the
// X-Subscription-Token header carrying the API key.
nlohmann::json searchBrave(const std::string& apiKey, const std::string& query, int max_results) {
    nlohmann::json results = nlohmann::json::array();
    if (apiKey.empty()) {
        PM_WARN("web_search(brave): no API key configured (web.search_api_key)");
        return results;
    }
    QUrl url(QStringLiteral("https://api.search.brave.com/res/v1/web/search"));
    QUrlQuery q;
    q.addQueryItem("q", QString::fromStdString(query));
    q.addQueryItem("count", QString::number(max_results));
    url.setQuery(q);

    nlohmann::json headers = {
        {"Accept", "application/json"},
        {"X-Subscription-Token", apiKey},
    };
    HttpResponse resp = tool_support::httpGet(url.toString(), headers);
    if (!resp.ok) {
        PM_WARN("web_search(brave): {}", resp.error.toStdString());
        return results;
    }
    nlohmann::json doc = nlohmann::json::parse(resp.body.toStdString(), nullptr, false);
    if (doc.is_discarded()) return results;

    const auto& web = doc.value("web", nlohmann::json::object());
    for (const auto& r : web.value("results", nlohmann::json::array())) {
        if (static_cast<int>(results.size()) >= max_results) break;
        results.push_back({
            {"title",   r.value("title", "")},
            {"url",     r.value("url", "")},
            {"snippet", r.value("description", "")},
        });
    }
    return results;
}

} // namespace

std::string WebSearchTool::name() const { return "web_search"; }
std::string WebSearchTool::description() const {
    return "Search the web and return a short list of result titles, URLs, and snippets. "
           "Follow up with fetch_page to read a result in full.";
}

nlohmann::json WebSearchTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"query",       {{"type", "string"},  {"description", "Search query"}}},
            {"max_results", {{"type", "integer"}, {"description", "Max results (default 5)"}}},
        }},
        {"required", {"query"}},
    };
}

ToolResult WebSearchTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const std::string query = args.value("query", "");
    int max_results = args.value("max_results", 5);
    if (max_results <= 0 || max_results > 20) max_results = 5;
    if (query.empty())
        return {false, {{"error", "query required"}}, "web_search: missing query"};

    // Backend + credentials from settings (Privacy/Settings UI writes these).
    const std::string backend = ctx.db->getSetting(keys::SearchBackend, "ddg");
    const std::string apiKey  = ctx.db->getSetting(keys::SearchApiKey, "");

    nlohmann::json results;
    if (backend == "searxng")      results = searchSearxng(apiKey, query, max_results);
    else if (backend == "brave")   results = searchBrave(apiKey, query, max_results);
    else                           results = searchDuckDuckGo(query, max_results);

    const size_t n = results.size();
    nlohmann::json content = {
        {"backend", backend},
        {"query", query},
        {"results", std::move(results)},
    };
    if (n == 0)
        return {true, std::move(content), "web_search: no results for \"" + query + "\""};
    return {true, std::move(content),
            "Found " + std::to_string(n) + " result(s) for \"" + query + "\""};
}

} // namespace polymath
