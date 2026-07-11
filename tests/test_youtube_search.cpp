// B1 — youtube_search: fixture-driven parser tests. No network I/O: the
// Innertube/ytInitialData JSON-walking parser is pure (nlohmann::json in,
// nlohmann::json out), so it's exercised directly against a recorded fixture
// (tests/fixtures/youtube_search.json) plus a couple of small inline cases
// for the ytInitialData HTML-scrape path and duration parsing.
#include "tools/youtube_search.h"
#undef NDEBUG   // keep assert() active even in Release (otherwise the test is a no-op)
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace polymath;
using namespace polymath::youtube_search_internal;

namespace {

nlohmann::json loadFixture() {
#ifndef PM_YOUTUBE_FIXTURES
#error "PM_YOUTUBE_FIXTURES not defined by CMake"
#endif
    const std::string path = std::string(PM_YOUTUBE_FIXTURES) + "/youtube_search.json";
    std::ifstream f(path);
    assert(f.good() && "fixture file missing: tests/fixtures/youtube_search.json");
    std::ostringstream ss;
    ss << f.rdbuf();
    nlohmann::json doc = nlohmann::json::parse(ss.str(), nullptr, false);
    assert(!doc.is_discarded() && "fixture is not valid JSON");
    return doc;
}

} // namespace

int main() {
    // --- duration parsing -----------------------------------------------------
    assert(parseDurationSeconds("3:45") == 225);
    assert(parseDurationSeconds("1:02:03") == 3723);
    assert(parseDurationSeconds("0:09") == 9);
    assert(parseDurationSeconds("") == 0);
    assert(parseDurationSeconds("LIVE") == 0);   // no digits at all

    // --- Innertube-shaped fixture: videoRenderer walk -------------------------
    nlohmann::json doc = loadFixture();
    nlohmann::json videos = extractVideosFromJson(doc, 6);
    assert(videos.is_array());
    assert(videos.size() == 3);   // 2 videos + 1 live; the interleaved channelRenderer is skipped

    const auto& v0 = videos[0];
    assert(v0["videoId"] == "aBcD12345_x");
    assert(v0["title"] == "Lofi Hip Hop Radio - beats to relax/study to");
    assert(v0["channel"] == "Lofi Girl");
    assert(v0["durationSec"] == 3723);          // "1:02:03"
    assert(v0["views"] == "12M views");          // shortViewCountText preferred
    assert(v0["publishedText"] == "3 years ago");
    assert(v0["thumbnailUrl"] == "https://i.ytimg.com/vi/aBcD12345_x/hqdefault.jpg");  // largest (last) thumb
    assert(v0["watchUrl"] == "https://www.youtube.com/watch?v=aBcD12345_x");

    const auto& v1 = videos[1];
    assert(v1["videoId"] == "xyz9876543A");
    assert(v1["channel"] == "History Byte");
    assert(v1["durationSec"] == 225);           // "3:45"
    assert(v1["views"] == "845K views");

    // Live video: no lengthText -> durationSec defaults to 0; no
    // shortViewCountText -> falls back to viewCountText, which here uses the
    // multi-"runs" form ("12,345" + " watching") rather than simpleText.
    const auto& v2 = videos[2];
    assert(v2["videoId"] == "live12345678");
    assert(v2["durationSec"] == 0);
    assert(v2["views"] == "12,345 watching");
    assert(v2["publishedText"] == "");           // missing field tolerated, defaults to ""

    // max_results truncates.
    nlohmann::json capped = extractVideosFromJson(doc, 2);
    assert(capped.size() == 2);

    // Empty/garbage doc degrades to an empty array, not a crash.
    nlohmann::json empty = extractVideosFromJson(nlohmann::json::object(), 6);
    assert(empty.is_array() && empty.empty());

    // Dedup: the same videoRenderer appearing twice (e.g. a continuation
    // reshowing a result) collapses to one entry, first-seen order kept.
    // Custom raw-string delimiter (JSON): the text below contains a literal
    // `)"` sequence ("...(ignored)"...") that would otherwise prematurely
    // close a plain R"(...)" literal.
    nlohmann::json dupDoc = nlohmann::json::parse(R"JSON({
        "a": {"videoRenderer": {"videoId": "dup1", "title": {"simpleText": "First"}}},
        "b": {"videoRenderer": {"videoId": "dup1", "title": {"simpleText": "Second (ignored)"}}},
        "c": {"videoRenderer": {"videoId": "dup2", "title": {"simpleText": "Third"}}}
    })JSON");
    nlohmann::json deduped = extractVideosFromJson(dupDoc, 10);
    assert(deduped.size() == 2);
    assert(deduped[0]["title"] == "First");

    // --- ytInitialData HTML scrape --------------------------------------------
    // Braces embedded inside a JSON string value (here, in a title) must not
    // truncate the balanced-brace scan early.
    const std::string html =
        "<html><head></head><body><script>var ytInitialData = "
        R"({"ok":true,"note":"weird } inside a string; still fine","n":{"deep":1}};)"
        "</script>more page text</body></html>";
    const std::string blob = extractYtInitialDataJson(html);
    assert(!blob.empty());
    nlohmann::json parsed = nlohmann::json::parse(blob, nullptr, false);
    assert(!parsed.is_discarded());
    assert(parsed["ok"] == true);
    assert(parsed["n"]["deep"] == 1);

    // window["ytInitialData"] = {...}; form also parses.
    const std::string html2 =
        R"(<script>window["ytInitialData"] = {"hello":"world"};</script>)";
    const std::string blob2 = extractYtInitialDataJson(html2);
    assert(!blob2.empty());
    nlohmann::json parsed2 = nlohmann::json::parse(blob2, nullptr, false);
    assert(!parsed2.is_discarded());
    assert(parsed2["hello"] == "world");

    // Missing marker -> "".
    assert(extractYtInitialDataJson("<html>no data here</html>").empty());

    std::puts("test_youtube_search: OK");
    return 0;
}
