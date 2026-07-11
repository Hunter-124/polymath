#pragma once
//
// youtube_search — keyless YouTube search. Primary path: POST the public
// Innertube web-client search endpoint (no API key). Fallback: GET the
// results page and scrape the `ytInitialData` JSON blob out of the HTML.
// Both paths share the same `videoRenderer`-walking parser. Implementation
// in youtube_search.cpp.
//
#include "i_tool.h"

namespace polymath {

class YoutubeSearchTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

// Pure parsing helpers, exposed so tests can exercise them against a stored
// fixture without touching the network (no QNetworkAccessManager involved).
namespace youtube_search_internal {

// Walks `doc` for `videoRenderer` nodes (present anywhere in the tree — the
// exact nesting differs between the Innertube search response and the
// ytInitialData blob scraped off the results page, so we don't hardcode a
// path) and returns up to `max_results` compact video objects:
// {videoId, title, channel, durationSec, views, publishedText, thumbnailUrl,
// watchUrl}. Tolerates missing fields (they default to "" / 0). Dedupes by
// videoId, preserving first-seen order.
nlohmann::json extractVideosFromJson(const nlohmann::json& doc, int max_results);

// Pulls the `ytInitialData` JSON object out of a YouTube results-page HTML
// document (handles both `var ytInitialData = {...};` and
// `window["ytInitialData"] = {...};` forms, brace-balanced so braces inside
// quoted strings don't truncate it early). Returns "" if not found.
std::string extractYtInitialDataJson(const std::string& html);

// "mm:ss" / "h:mm:ss" -> total seconds. Returns 0 for empty/unparseable text
// (e.g. live videos report "LIVE" instead of a length).
int parseDurationSeconds(const std::string& text);

} // namespace youtube_search_internal

} // namespace polymath
