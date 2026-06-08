#include "finder.h"

#include "visual_memory.h"
#include "inference_manager.h"
#include "database.h"
#include "logging.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace polymath {

Finder::Finder(InferenceManager& inf, VisualMemory& mem, Database& db)
    : inf_(inf), mem_(mem), db_(db) {}

std::string Finder::cameraName(int camera_id) {
    std::string name;
    db_.query("SELECT name FROM cameras WHERE id=?1", {camera_id},
              [&](const Row& r) { name = r.text(0); });
    if (name.empty())
        name = "camera " + std::to_string(camera_id);
    return name;
}

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Friendly "3 minutes ago" / "just now" phrasing for the answer.
std::string humanizeAge(TimePoint ts) {
    const int64_t secs = to_unix(Clock::now()) - to_unix(ts);
    if (secs < 5)    return "just now";
    if (secs < 60)   return std::to_string(secs) + " seconds ago";
    if (secs < 3600) return std::to_string(secs / 60) + " minute(s) ago";
    if (secs < 86400) return std::to_string(secs / 3600) + " hour(s) ago";
    return std::to_string(secs / 86400) + " day(s) ago";
}

} // namespace

std::string Finder::askFrame(const Frame& frame, const std::string& query) {
    if (frame.jpeg.empty())
        return {};

    // Constrain the VLM to a yes/no + location so we can parse a decisive answer.
    const std::string prompt =
        "You are reviewing a single still frame from a home security camera. "
        "Question: is there clearly a \"" + query + "\" visible in this image? "
        "If yes, answer starting with the word YES followed by a short phrase "
        "describing where it is (e.g. 'YES on the kitchen counter'). "
        "If you are not sure or it is not present, answer with only the word NO.";

    std::string reply;
    try {
        reply = inf_.describeImage(frame, prompt);
    } catch (const std::exception& e) {
        PM_WARN("Finder: describeImage failed: {}", e.what());
        return {};
    }

    const std::string l = lower(reply);
    // Decisive affirmative only: the reply must begin with "yes". Anything else
    // (including "no" / hedging) is treated as not-present so we keep scanning.
    if (l.rfind("yes", 0) != 0)
        return {};

    // Strip the leading "yes" token and any separators for a clean location phrase.
    std::string where = reply.substr(3 <= reply.size() ? 3 : reply.size());
    size_t i = 0;
    while (i < where.size() && (std::isspace((unsigned char)where[i]) ||
           where[i] == ',' || where[i] == ':' || where[i] == '.'))
        ++i;
    where = where.substr(i);
    return where.empty() ? std::string("in view") : where;
}

FindObjectResult Finder::find(const std::string& query, size_t max_frames) {
    FindObjectResult out;
    out.query = QString::fromStdString(query);
    out.camera_id = -1;
    out.ts = 0;

    if (max_frames == 0)
        max_frames = default_max_frames_;

    auto frames = mem_.recent(max_frames);
    if (frames.empty()) {
        out.answer = QStringLiteral("No recent camera frames to search.");
        return out;
    }

    PM_INFO("Finder: searching {} recent frames for '{}'", frames.size(), query);

    for (const auto& snap : frames) {
        const std::string where = askFrame(snap.frame, query);
        if (!where.empty()) {
            const std::string cam = cameraName(snap.frame.camera_id);
            const std::string answer =
                "Last seen on " + cam + " " + humanizeAge(snap.frame.ts) +
                " — " + where;
            out.answer = QString::fromStdString(answer);
            out.camera_id = snap.frame.camera_id;
            out.ts = to_unix(snap.frame.ts);
            PM_INFO("Finder: '{}' -> {}", query, answer);
            return out;
        }
    }

    out.answer = QString::fromStdString(
        "I couldn't find \"" + query + "\" in the recent camera footage.");
    return out;
}

} // namespace polymath
