#include "fetch_page.h"
#include "tool_support.h"
#include "logging.h"

// fetch_page — fetch a URL and extract its readable text (readability-style:
// drop script/style/markup, decode entities, collapse whitespace). Returns the
// page title + cleaned body, truncated so it fits comfortably in context.

namespace polymath {

std::string FetchPageTool::name() const { return "fetch_page"; }
std::string FetchPageTool::description() const {
    return "Fetch a web page by URL and return its readable text content (title + body). "
           "Use after web_search to read a result in full.";
}

nlohmann::json FetchPageTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"url",       {{"type", "string"},  {"description", "Absolute http(s) URL to fetch"}}},
            {"max_chars", {{"type", "integer"}, {"description", "Truncate body to N chars (default 8000)"}}},
        }},
        {"required", {"url"}},
    };
}

ToolResult FetchPageTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string url = args.value("url", "");
    size_t max_chars = static_cast<size_t>(args.value("max_chars", 8000));
    if (max_chars == 0 || max_chars > 64000) max_chars = 8000;
    if (url.empty())
        return {false, {{"error", "url required"}}, "fetch_page: missing url"};
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
        return {false, {{"error", "url must be http(s)"}}, "fetch_page: non-http url"};

    nlohmann::json headers = {{"Accept", "text/html,application/xhtml+xml"}};
    tool_support::HttpResponse resp =
        tool_support::httpGet(QString::fromStdString(url), headers, 25000);

    if (!resp.ok) {
        return {false,
                {{"error", resp.error.toStdString()}, {"status", resp.status}, {"url", url}},
                "fetch_page: failed (" + resp.error.toStdString() + ")"};
    }

    // Only attempt readability extraction on HTML; otherwise return raw text.
    std::string title;
    std::string text;
    if (resp.contentType.contains("html") || resp.contentType.isEmpty()) {
        title = tool_support::htmlTitle(resp.body);
        text  = tool_support::htmlToText(resp.body, max_chars);
    } else {
        text = tool_support::htmlToText(resp.body, max_chars);   // harmless on plain text
    }

    nlohmann::json content = {
        {"url", resp.finalUrl.toStdString()},
        {"status", resp.status},
        {"title", title},
        {"content", text},
    };
    const std::string label = title.empty() ? url : title;
    return {true, std::move(content), "Fetched \"" + label + "\" (" +
                                          std::to_string(text.size()) + " chars)"};
}

} // namespace polymath
