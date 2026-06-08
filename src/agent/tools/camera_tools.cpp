#include "camera_tools.h"
#include "tool_support.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"

#include <QString>

// camera_snapshot / who_is_home — answer "home presence" questions from the
// `events` table (kind=person|face, with optional resolved user_id) and surface
// the most recent camera thumbnail. The VisionService owns live capture and
// writes `events`/thumbnails; these tools read that durable record (and post a
// Notice so the UI can show the snapshot), rather than blocking on a camera.

namespace polymath {

namespace {

// Resolve a camera by name (case-insensitive) or by numeric id. Returns -1 if
// not found / not specified. Fills `outName` with the canonical name.
int64_t resolveCamera(Database& db, const nlohmann::json& args, std::string& outName) {
    if (args.contains("camera_id") && args["camera_id"].is_number()) {
        const int64_t id = args["camera_id"].get<int64_t>();
        db.query("SELECT name FROM cameras WHERE id=?1", {id},
                 [&](const Row& r) { outName = r.text(0); });
        return outName.empty() ? -1 : id;
    }
    const std::string name = args.value("camera", "");
    if (name.empty()) return -1;
    int64_t found = -1;
    db.query("SELECT id,name FROM cameras WHERE LOWER(name)=LOWER(?1)", {name},
             [&](const Row& r) { found = r.i64(0); outName = r.text(1); });
    return found;
}

} // namespace

// --- camera_snapshot --------------------------------------------------------

std::string CameraSnapshotTool::name() const { return "camera_snapshot"; }
std::string CameraSnapshotTool::description() const {
    return "Get the most recent snapshot from a camera (by name or id, default: any). "
           "Returns the thumbnail path and what was last seen.";
}

nlohmann::json CameraSnapshotTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"camera",    {{"type", "string"},  {"description", "Camera name (optional)"}}},
            {"camera_id", {{"type", "integer"}, {"description", "Camera id (optional)"}}},
        }},
    };
}

ToolResult CameraSnapshotTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    std::string camName;
    const int64_t camId = resolveCamera(*ctx.db, args, camName);
    if ((args.contains("camera") || args.contains("camera_id")) && camId < 0)
        return {false, {{"error", "camera not found"}}, "camera_snapshot: unknown camera"};

    // Most recent event carrying a thumbnail (optionally for the chosen camera).
    std::string sql =
        "SELECT camera_id,kind,label,thumb_path,ts FROM events "
        "WHERE thumb_path != ''";
    std::vector<nlohmann::json> params;
    if (camId >= 0) { sql += " AND camera_id=?1"; params.push_back(camId); }
    sql += " ORDER BY ts DESC LIMIT 1";

    nlohmann::json snap;
    ctx.db->query(sql, params, [&](const Row& r) {
        snap = {
            {"camera_id", r.i64(0)},
            {"kind", r.text(1)},
            {"label", r.text(2)},
            {"thumb_path", r.text(3)},
            {"ts", r.i64(4)},
        };
    });

    if (snap.is_null()) {
        return {true, {{"camera", camName}, {"snapshot", nullptr}},
                "camera_snapshot: no recent frame available"};
    }

    // Surface the snapshot path to the UI as a notice (a real preview is the
    // UI's job; we provide the durable thumbnail path).
    EventBus::instance().publishNotice(
        {"info", "agent",
         QStringLiteral("Snapshot: %1").arg(QString::fromStdString(
             snap.value("thumb_path", std::string{})))});

    nlohmann::json content = {{"camera", camName}, {"snapshot", snap}};
    return {true, std::move(content), "Latest snapshot retrieved"};
}

// --- who_is_home ------------------------------------------------------------

std::string WhoIsHomeTool::name() const { return "who_is_home"; }
std::string WhoIsHomeTool::description() const {
    return "Report who appears to be home, based on recently recognized people from the "
           "cameras within the last few minutes.";
}

nlohmann::json WhoIsHomeTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"within_minutes", {{"type", "integer"},
                                {"description", "Look-back window in minutes (default 15)"}}},
        }},
    };
}

ToolResult WhoIsHomeTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    int within = args.value("within_minutes", 15);
    if (within <= 0 || within > 1440) within = 15;
    const int64_t since = tool_support::nowUnix() - static_cast<int64_t>(within) * 60;

    // Distinct recognized users from recent person/face events (named via users).
    nlohmann::json people = nlohmann::json::array();
    ctx.db->query(
        "SELECT u.name, MAX(e.ts) AS last_seen "
        "FROM events e JOIN users u ON e.user_id = u.id "
        "WHERE e.user_id IS NOT NULL AND e.ts >= ?1 "
        "  AND e.kind IN ('person','face') "
        "GROUP BY u.id ORDER BY last_seen DESC",
        {since}, [&](const Row& r) {
            people.push_back({{"name", r.text(0)}, {"last_seen", r.i64(1)}});
        });

    // Were there unidentified people too?
    int64_t anonCount = 0;
    ctx.db->query(
        "SELECT COUNT(*) FROM events "
        "WHERE user_id IS NULL AND ts >= ?1 AND kind IN ('person','face')",
        {since}, [&](const Row& r) { anonCount = r.i64(0); });

    nlohmann::json content = {
        {"within_minutes", within},
        {"people", people},
        {"unidentified_sightings", anonCount},
    };

    std::string summary;
    if (people.empty() && anonCount == 0) {
        summary = "No one detected at home in the last " + std::to_string(within) + " min";
    } else if (people.empty()) {
        summary = "Someone unidentified seen recently (" + std::to_string(anonCount) + " sighting(s))";
    } else {
        summary = std::to_string(people.size()) + " known person(s) seen recently";
    }
    return {true, std::move(content), summary};
}

} // namespace polymath
