#include "http_router.h"

#include "auth.h"
#include "bridge.h"
#include "config.h"
#include "database.h"
#include "fabric_service.h"
#include "gateway_db.h"
#include "json_map.h"
#include "logging.h"
#include "paths.h"
#include "types.h"

#include <nlohmann/json.hpp>

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <filesystem>

namespace polymath {

using nlohmann::json;
namespace jm = json_map;

// ─── Response builders ──────────────────────────────────────────────────────

Response Response::json(int status, const std::string& jsonBody) {
    Response r;
    r.status = status;
    r.headers["content-type"] = "application/json";
    r.body = QByteArray::fromStdString(jsonBody);
    return r;
}

Response Response::text(int status, const std::string& msg) {
    Response r;
    r.status = status;
    r.headers["content-type"] = "text/plain; charset=utf-8";
    r.body = QByteArray::fromStdString(msg);
    return r;
}

Response Response::bytes(int status, const char* contentType, QByteArray data) {
    Response r;
    r.status = status;
    r.headers["content-type"] = contentType;
    r.body = std::move(data);
    return r;
}

Response Response::error(int status, const std::string& code, const std::string& message) {
    // Qualify nlohmann::json explicitly: inside a Response member, unqualified
    // `json` resolves to the static Response::json builder, not the JSON type.
    return Response::json(status, nlohmann::json{{"error", code}, {"message", message}}.dump());
}

Response Response::noContent() {
    Response r;
    r.status = 204;
    return r;
}

// ─── construction ───────────────────────────────────────────────────────────

HttpRouter::HttpRouter(IAssistantBridge& bridge, Database& db, Config& cfg, Auth& auth,
                       FabricService* fabric)
    : bridge_(bridge), db_(db), cfg_(cfg), auth_(auth), fabric_(fabric) {
    start_unix_ = jm::nowUnix();
}

// ─── small helpers ──────────────────────────────────────────────────────────

HttpRouter::Parsed HttpRouter::parsePath(const QString& path) {
    Parsed p;
    const QUrl url(path);   // QUrl parses the query for us
    QString pathOnly = url.path();
    // Trim trailing slash (so "/api/v1/tasks/" == "/api/v1/tasks").
    while (pathOnly.size() > 1 && pathOnly.endsWith('/'))
        pathOnly.chop(1);
    const QStringList segs =
        pathOnly.split('/', Qt::SkipEmptyParts);
    for (const QString& s : segs) p.segments.push_back(s);

    const QUrlQuery q(url.query());
    for (const auto& kv : q.queryItems(QUrl::FullyDecoded))
        p.query.insert(kv.first, kv.second);
    return p;
}

QString HttpRouter::extractToken(const std::map<std::string, std::string>& headers,
                                 const QHash<QString, QString>& query) {
    // Header first (Authorization: Bearer <t>); fall back to ?token= for media.
    auto it = headers.find("authorization");
    if (it != headers.end() && !it->second.empty())
        return QString::fromStdString(it->second);
    if (query.contains(QStringLiteral("token")))
        return query.value(QStringLiteral("token"));
    return QString();
}

// Read the JSON request body (object); returns false (and fills err) on bad JSON.
static bool parseBody(const QByteArray& body, json& out, Response& err) {
    if (body.isEmpty()) { out = json::object(); return true; }
    try {
        out = json::parse(body.toStdString());
        return true;
    } catch (const std::exception& e) {
        err = Response::error(400, "bad_json", e.what());
        return false;
    }
}

// ─── dispatch ───────────────────────────────────────────────────────────────

Response HttpRouter::handle(const QString& method,
                            const QString& path,
                            const std::map<std::string, std::string>& headers,
                            const QByteArray& body) {
    Request req;
    req.method  = method.toUpper();
    req.path    = path;
    req.headers = headers;
    req.body    = body;

    const Parsed p = parsePath(path);

    // Expect /api/v1/<resource>...; anything else is unknown.
    if (p.segments.size() < 2 || p.segments[0] != QLatin1String("api") ||
        p.segments[1] != QLatin1String("v1")) {
        return Response::error(404, "not_found", "unknown path");
    }
    const QString resource = p.segments.size() > 2 ? p.segments[2] : QString();

    // --- auth gate -------------------------------------------------------
    // Open routes: POST /pair and GET /status.  Everything else needs a token.
    const bool openRoute =
        (resource == QLatin1String("pair")) ||
        (resource == QLatin1String("status"));

    QString deviceId, role;
    if (!openRoute) {
        const QString tok = extractToken(headers, p.query);
        auto claims = auth_.verifyToken(tok);
        if (!claims) {
            Response r = Response::error(401, "unauthorized", "missing or invalid token");
            r.headers["www-authenticate"] = "Bearer";
            return r;
        }
        deviceId = claims->device_id;
        role     = claims->role;
    }

    // --- route -----------------------------------------------------------
    if (resource == QLatin1String("pair"))   return routePair(req);
    if (resource == QLatin1String("status")) return routeStatus(req);
    if (resource == QLatin1String("me"))     return routeMe(req, deviceId, role);

    if (resource == QLatin1String("chat")) {
        // /chat  vs  /chat/history
        if (p.segments.size() >= 4 && p.segments[3] == QLatin1String("history"))
            return routeChatHistory(req, p);
        return routeChat(req, p);
    }

    if (resource == QLatin1String("cameras"))     return routeCameras(req, p, extractToken(headers, p.query));
    if (resource == QLatin1String("find-object")) return routeFindObject(req);

    if (resource == QLatin1String("tasks"))     return routeTasks(req, p);
    if (resource == QLatin1String("reminders")) return routeReminders(req, p);
    if (resource == QLatin1String("shopping"))  return routeShopping(req, p);

    if (resource == QLatin1String("timeline")) {
        // /timeline  vs  /timeline/:id/thumb
        if (p.segments.size() >= 5 && p.segments[4] == QLatin1String("thumb"))
            return routeTimelineThumb(req, p.segments[3].toInt());
        return routeTimeline(req, p);
    }
    if (resource == QLatin1String("memory"))        return routeMemory(req, p);
    if (resource == QLatin1String("personalities")) return routePersonalities(req, p);
    if (resource == QLatin1String("models"))        return routeModels(req);
    if (resource == QLatin1String("settings"))      return routeSettings(req, p);
    if (resource == QLatin1String("devices"))       return routeDevices(req, p);

    // --- device fabric (v2) ----------------------------------------------
    if (resource == QLatin1String("fabric"))      return routeFabric(req, p);
    if (resource == QLatin1String("instruments")) return routeInstruments(req, p);
    if (resource == QLatin1String("lab"))         return routeLab(req, p);

    // /events is the WebSocket upgrade path, handled by http_server, not here.
    if (resource == QLatin1String("events"))
        return Response::error(426, "upgrade_required", "connect via WebSocket");

    return Response::error(404, "not_found", "unknown resource");
}

// ─── /pair (open) ───────────────────────────────────────────────────────────

Response HttpRouter::routePair(const Request& req) {
    if (req.method != QLatin1String("POST"))
        return Response::error(405, "method_not_allowed", "use POST");

    json b; Response err;
    if (!parseBody(req.body, b, err)) return err;

    const QString code = QString::fromStdString(b.value("code", std::string()));
    const QString name = QString::fromStdString(b.value("device_name", std::string("device")));
    const QString pubkey   = QString::fromStdString(b.value("pubkey", std::string()));
    const QString platform = QString::fromStdString(b.value("platform", std::string()));

    if (code.isEmpty())
        return Response::error(400, "missing_code", "pair code required");
    if (!auth_.verifyPairCode(code))
        return Response::error(403, "invalid_code", "pair code invalid or expired");

    // First paired device is the owner; this could be tightened later (e.g. the
    // desktop chooses the role when generating the code).
    const QString role     = QStringLiteral("owner");
    const QString deviceId = auth_.createDevice(name, platform, pubkey, role);
    const QString token    = auth_.issueToken(deviceId, role);

    json resp{
        {"token",        token.toStdString()},
        {"device_id",    deviceId.toStdString()},
        {"role",         role.toStdString()},
        {"home_id",      GatewayDb::homeId(db_)},
        {"relay_url",    GatewayDb::relayUrl(db_)},
        {"capabilities", jm::serverCapabilities()},
    };
    return Response::json(200, resp.dump());
}

// ─── /me ────────────────────────────────────────────────────────────────────

Response HttpRouter::routeMe(const Request& req, const QString& deviceId, const QString& role) {
    if (req.method != QLatin1String("GET"))
        return Response::error(405, "method_not_allowed", "use GET");
    json resp{
        {"device_id",    deviceId.toStdString()},
        {"role",         role.toStdString()},
        {"home_id",      GatewayDb::homeId(db_)},
        {"capabilities", jm::serverCapabilities()},
    };
    return Response::json(200, resp.dump());
}

// ─── /status (open) ─────────────────────────────────────────────────────────

Response HttpRouter::routeStatus(const Request& req) {
    if (req.method != QLatin1String("GET"))
        return Response::error(405, "method_not_allowed", "use GET");
    const int64_t uptime = jm::nowUnix() - start_unix_;
    json st = jm::serverStatus(db_,
                               bridge_.listening(),
                               bridge_.activePersonality().toStdString(),
                               bridge_.modelStatus().toStdString(),
                               bridge_.ttsReady(),
                               bridge_.ttsStatus().toStdString(),
                               uptime);
    return Response::json(200, st.dump());
}

// ─── /chat ──────────────────────────────────────────────────────────────────

Response HttpRouter::routeChat(const Request& req, const Parsed&) {
    if (req.method != QLatin1String("POST"))
        return Response::error(405, "method_not_allowed", "use POST");
    json b; Response err;
    if (!parseBody(req.body, b, err)) return err;

    const QString text = QString::fromStdString(b.value("text", std::string()));
    if (text.trimmed().isEmpty())
        return Response::error(400, "empty_text", "text is required");

    // Optional per-turn personality override.
    if (b.contains("personality") && b["personality"].is_string()) {
        const std::string pn = b["personality"].get<std::string>();
        if (!pn.empty()) bridge_.setPersonality(QString::fromStdString(pn));
    }

    // Returns the request_id the client correlates with `token` WS events.
    const QString rid = bridge_.sendChat(text);
    return Response::json(200, json{{"request_id", rid.toStdString()}}.dump());
}

// GET /chat/history?limit=N — recent transcripts as ChatMessageDTO[].
Response HttpRouter::routeChatHistory(const Request& req, const Parsed& p) {
    if (req.method != QLatin1String("GET"))
        return Response::error(405, "method_not_allowed", "use GET");
    int limit = p.query.value(QStringLiteral("limit"), QStringLiteral("100")).toInt();
    if (limit <= 0 || limit > 500) limit = 100;

    // The app stores command/ambient turns in `transcripts`.  We surface them as
    // chat history; speaker NULL => assistant, else user (best-effort mapping —
    // the schema has no explicit role column).
    // Take the most-recent N (inner query), then emit oldest-first so the client
    // renders the conversation top-to-bottom without an in-memory reverse.
    json arr = json::array();
    db_.query(
        "SELECT id, text, speaker, ts FROM "
        "(SELECT id, text, speaker, ts FROM transcripts ORDER BY ts DESC LIMIT ?1) "
        "ORDER BY ts ASC",
        {limit},
        [&](const Row& r) {
            arr.push_back(json{
                {"id",      r.i64(0)},
                {"role",    r.isNull(2) ? "assistant" : "user"},
                {"content", r.text(1)},
                {"ts",      r.i64(3)},
            });
        });
    return Response::json(200, arr.dump());
}

// ─── /cameras + media proxy ─────────────────────────────────────────────────

Response HttpRouter::routeCameras(const Request& req, const Parsed& p, const QString& /*token*/) {
    // /cameras                       -> list
    // /cameras/:id/snapshot|stream   -> media proxy (GET)
    // /cameras/:id/events            -> edge detection+clip ingest (POST, fabric)
    // /cameras/:id/frame             -> live UI tile push (POST, fabric)
    if (p.segments.size() >= 5) {
        const int cameraId = p.segments[3].toInt();
        const QString kind = p.segments[4];
        if (kind == QLatin1String("events")) return routeCameraEvents(req, cameraId);
        if (kind == QLatin1String("frame"))  return routeCameraFrame(req, cameraId);
        if (req.method != QLatin1String("GET"))
            return Response::error(405, "method_not_allowed", "use GET");
        return routeCameraMedia(req, cameraId, kind);
    }

    if (req.method != QLatin1String("GET"))
        return Response::error(405, "method_not_allowed", "use GET");
    json arr = json::array();
    db_.query("SELECT id, name, location, enabled FROM cameras ORDER BY id ASC", {},
              [&](const Row& r) { arr.push_back(jm::cameraFromRow(r)); });
    return Response::json(200, arr.dump());
}

Response HttpRouter::routeCameraMedia(const Request&, int cameraId, const QString& kind) {
    // Snapshot: serve the most recent thumbnail captured for this camera from
    // the events table (the vision service writes thumb_path).  This keeps the
    // raw camera URL off the wire and works over the relay.
    //   The live MJPEG `stream` is best delivered via the WS `frame` events
    //   (subscribe to the camera); a true multipart MJPEG proxy is a finishing
    //   detail (see README "stream proxy").  For now /stream returns the latest
    //   frame too, so an <img> still renders.
    std::string thumb;
    db_.query(
        "SELECT thumb_path FROM events "
        "WHERE camera_id=?1 AND thumb_path<>'' ORDER BY ts DESC LIMIT 1",
        {cameraId},
        [&](const Row& r) { thumb = r.text(0); });

    if (thumb.empty())
        return Response::error(404, "no_frame", "no snapshot available for this camera");

    Q_UNUSED(kind);
    return serveJpegFile(thumb);
}

Response HttpRouter::routeFindObject(const Request& req) {
    if (req.method != QLatin1String("POST"))
        return Response::error(405, "method_not_allowed", "use POST");
    json b; Response err;
    if (!parseBody(req.body, b, err)) return err;
    const QString query = QString::fromStdString(b.value("query", std::string()));
    if (query.trimmed().isEmpty())
        return Response::error(400, "empty_query", "query is required");
    // Fire-and-forget: the answer arrives later as a `find_object` WS event.
    bridge_.findObject(query);
    return Response::json(202, json{{"accepted", true}}.dump());
}

// ─── /tasks ─────────────────────────────────────────────────────────────────

Response HttpRouter::routeTasks(const Request& req, const Parsed& p) {
    const bool hasId = p.segments.size() >= 4;
    const int64_t id = hasId ? p.segments[3].toLongLong() : 0;

    if (!hasId && req.method == QLatin1String("GET")) {
        json arr = json::array();
        db_.query(
            "SELECT id,type,params_json,priority,status,result_json,created_at,updated_at "
            "FROM tasks ORDER BY created_at DESC LIMIT 500",
            {}, [&](const Row& r) { arr.push_back(jm::taskFromRow(r)); });
        return Response::json(200, arr.dump());
    }

    if (!hasId && req.method == QLatin1String("POST")) {
        json b; Response err;
        if (!parseBody(req.body, b, err)) return err;
        const std::string type = b.value("type", std::string());
        if (type.empty()) return Response::error(400, "missing_type", "type is required");
        const std::string params = b.contains("params") ? b["params"].dump() : "{}";
        const int priority = b.value("priority", 0);
        const int64_t now = jm::nowUnix();
        const int64_t newId = db_.exec(
            "INSERT INTO tasks(type,params_json,priority,status,created_at,updated_at) "
            "VALUES(?1,?2,?3,'queued',?4,?4)",
            {type, params, priority, now});
        // Echo the created row.
        json created;
        db_.query(
            "SELECT id,type,params_json,priority,status,result_json,created_at,updated_at "
            "FROM tasks WHERE id=?1", {newId},
            [&](const Row& r) { created = jm::taskFromRow(r); });
        return Response::json(201, created.dump());
    }

    if (hasId && req.method == QLatin1String("GET")) {
        json found = json::value_t::null;
        db_.query(
            "SELECT id,type,params_json,priority,status,result_json,created_at,updated_at "
            "FROM tasks WHERE id=?1", {id},
            [&](const Row& r) { found = jm::taskFromRow(r); });
        if (found.is_null()) return Response::error(404, "not_found", "task not found");
        return Response::json(200, found.dump());
    }

    if (hasId && req.method == QLatin1String("DELETE")) {
        // Cancel rather than hard-delete a running task; mark canceled.
        db_.exec("UPDATE tasks SET status='canceled', updated_at=?1 WHERE id=?2",
                 {jm::nowUnix(), id});
        return Response::noContent();
    }

    return Response::error(405, "method_not_allowed", "unsupported method for /tasks");
}

// ─── /reminders ─────────────────────────────────────────────────────────────

Response HttpRouter::routeReminders(const Request& req, const Parsed& p) {
    const bool hasId = p.segments.size() >= 4;
    const int64_t id = hasId ? p.segments[3].toLongLong() : 0;

    if (!hasId && req.method == QLatin1String("GET")) {
        json arr = json::array();
        db_.query(
            "SELECT id,text,due_at,rrule,condition,fired,created_at "
            "FROM reminders ORDER BY (due_at IS NULL), due_at ASC, created_at DESC LIMIT 500",
            {}, [&](const Row& r) { arr.push_back(jm::reminderFromRow(r)); });
        return Response::json(200, arr.dump());
    }

    if (!hasId && req.method == QLatin1String("POST")) {
        json b; Response err;
        if (!parseBody(req.body, b, err)) return err;
        const std::string text = b.value("text", std::string());
        if (text.empty()) return Response::error(400, "missing_text", "text is required");
        const std::string rrule = b.value("rrule", std::string());
        const std::string cond  = b.value("condition", std::string());
        const int64_t now = jm::nowUnix();
        std::vector<nlohmann::json> params;
        std::string sql;
        if (b.contains("due_at") && b["due_at"].is_number()) {
            sql = "INSERT INTO reminders(text,due_at,rrule,condition,created_at) VALUES(?1,?2,?3,?4,?5)";
            params = {text, b["due_at"].get<int64_t>(), rrule, cond, now};
        } else {
            sql = "INSERT INTO reminders(text,rrule,condition,created_at) VALUES(?1,?2,?3,?4)";
            params = {text, rrule, cond, now};
        }
        const int64_t newId = db_.exec(sql, params);
        json created;
        db_.query("SELECT id,text,due_at,rrule,condition,fired,created_at FROM reminders WHERE id=?1",
                  {newId}, [&](const Row& r) { created = jm::reminderFromRow(r); });
        return Response::json(201, created.dump());
    }

    if (hasId && req.method == QLatin1String("DELETE")) {
        db_.exec("DELETE FROM reminders WHERE id=?1", {id});
        return Response::noContent();
    }

    return Response::error(405, "method_not_allowed", "unsupported method for /reminders");
}

// ─── /shopping ──────────────────────────────────────────────────────────────

Response HttpRouter::routeShopping(const Request& req, const Parsed& p) {
    const bool hasId = p.segments.size() >= 4;
    const int64_t id = hasId ? p.segments[3].toLongLong() : 0;

    if (!hasId && req.method == QLatin1String("GET")) {
        json arr = json::array();
        db_.query("SELECT id,item,quantity,done,created_at FROM shopping_items "
                  "ORDER BY done ASC, created_at DESC",
                  {}, [&](const Row& r) { arr.push_back(jm::shoppingItemFromRow(r)); });
        return Response::json(200, arr.dump());
    }

    if (!hasId && req.method == QLatin1String("POST")) {
        json b; Response err;
        if (!parseBody(req.body, b, err)) return err;
        const std::string item = b.value("item", std::string());
        if (item.empty()) return Response::error(400, "missing_item", "item is required");
        const std::string qty = b.value("quantity", std::string());
        // Route through the bridge so the desktop UI list stays in sync.  The
        // bridge insert may complete on another thread, so we can't rely on
        // reading it back synchronously; echo what we can and fill in the row if
        // it's already visible.
        bridge_.addShoppingItem(QString::fromStdString(item));
        if (!qty.empty()) {
            db_.exec("UPDATE shopping_items SET quantity=?1 WHERE id="
                     "(SELECT id FROM shopping_items WHERE item=?2 ORDER BY created_at DESC LIMIT 1)",
                     {qty, item});
        }
        json created = json::value_t::null;
        db_.query("SELECT id,item,quantity,done,created_at FROM shopping_items "
                  "WHERE item=?1 ORDER BY created_at DESC LIMIT 1",
                  {item}, [&](const Row& r) { created = jm::shoppingItemFromRow(r); });
        if (created.is_null()) {
            // Row not visible yet (async bridge insert in flight); return a
            // best-effort DTO so the client still gets a well-formed item.
            created = json{
                {"id",         0},
                {"item",       item},
                {"quantity",   qty},
                {"done",       false},
                {"created_at", jm::nowUnix()},
            };
        }
        return Response::json(201, created.dump());
    }

    if (hasId && (req.method == QLatin1String("PATCH") || req.method == QLatin1String("PUT"))) {
        json b; Response err;
        if (!parseBody(req.body, b, err)) return err;
        if (b.contains("done"))
            db_.exec("UPDATE shopping_items SET done=?1 WHERE id=?2",
                     {b["done"].get<bool>() ? 1 : 0, id});
        if (b.contains("quantity") && b["quantity"].is_string())
            db_.exec("UPDATE shopping_items SET quantity=?1 WHERE id=?2",
                     {b["quantity"].get<std::string>(), id});
        if (b.contains("item") && b["item"].is_string())
            db_.exec("UPDATE shopping_items SET item=?1 WHERE id=?2",
                     {b["item"].get<std::string>(), id});
        json updated = json::value_t::null;
        db_.query("SELECT id,item,quantity,done,created_at FROM shopping_items WHERE id=?1",
                  {id}, [&](const Row& r) { updated = jm::shoppingItemFromRow(r); });
        if (updated.is_null()) return Response::error(404, "not_found", "item not found");
        return Response::json(200, updated.dump());
    }

    if (hasId && req.method == QLatin1String("DELETE")) {
        db_.exec("DELETE FROM shopping_items WHERE id=?1", {id});
        return Response::noContent();
    }

    return Response::error(405, "method_not_allowed", "unsupported method for /shopping");
}

// ─── /timeline ──────────────────────────────────────────────────────────────

Response HttpRouter::routeTimeline(const Request& req, const Parsed& p) {
    if (req.method != QLatin1String("GET"))
        return Response::error(405, "method_not_allowed", "use GET");
    int limit = p.query.value(QStringLiteral("limit"), QStringLiteral("100")).toInt();
    if (limit <= 0 || limit > 500) limit = 100;
    // Optional ?before=<ts> cursor for paging older events.
    const bool hasBefore = p.query.contains(QStringLiteral("before"));
    const int64_t before = p.query.value(QStringLiteral("before")).toLongLong();

    json arr = json::array();
    if (hasBefore) {
        db_.query(
            "SELECT id,kind,camera_id,user_id,label,thumb_path,ts,clip_url,confidence FROM events "
            "WHERE ts < ?1 ORDER BY ts DESC LIMIT ?2",
            {before, limit}, [&](const Row& r) { arr.push_back(jm::timelineEventFromRow(r)); });
    } else {
        db_.query(
            "SELECT id,kind,camera_id,user_id,label,thumb_path,ts,clip_url,confidence FROM events "
            "ORDER BY ts DESC LIMIT ?1",
            {limit}, [&](const Row& r) { arr.push_back(jm::timelineEventFromRow(r)); });
    }
    return Response::json(200, arr.dump());
}

Response HttpRouter::routeTimelineThumb(const Request& req, int eventId) {
    if (req.method != QLatin1String("GET"))
        return Response::error(405, "method_not_allowed", "use GET");
    std::string thumb;
    db_.query("SELECT thumb_path FROM events WHERE id=?1", {eventId},
              [&](const Row& r) { thumb = r.text(0); });
    if (thumb.empty())
        return Response::error(404, "not_found", "no thumbnail for this event");
    return serveJpegFile(thumb);
}

// ─── /memory ────────────────────────────────────────────────────────────────

Response HttpRouter::routeMemory(const Request& req, const Parsed& p) {
    if (req.method == QLatin1String("GET")) {
        int limit = p.query.value(QStringLiteral("limit"), QStringLiteral("100")).toInt();
        if (limit <= 0 || limit > 500) limit = 100;
        // Optional ?q= text filter (vector search is the agent's job; here we do
        // a simple LIKE so the mobile UI can browse/filter notes).
        const QString q = p.query.value(QStringLiteral("q"));
        json arr = json::array();
        if (q.isEmpty()) {
            db_.query("SELECT id,kind,text,source,user_id,ts FROM memories "
                      "ORDER BY ts DESC LIMIT ?1",
                      {limit}, [&](const Row& r) { arr.push_back(jm::memoryFromRow(r)); });
        } else {
            db_.query("SELECT id,kind,text,source,user_id,ts FROM memories "
                      "WHERE text LIKE ?1 ORDER BY ts DESC LIMIT ?2",
                      {"%" + q.toStdString() + "%", limit},
                      [&](const Row& r) { arr.push_back(jm::memoryFromRow(r)); });
        }
        return Response::json(200, arr.dump());
    }

    if (req.method == QLatin1String("POST")) {
        json b; Response err;
        if (!parseBody(req.body, b, err)) return err;
        const std::string text = b.value("text", std::string());
        if (text.empty()) return Response::error(400, "missing_text", "text is required");
        const std::string kind = b.value("kind", std::string("note"));
        const int64_t now = jm::nowUnix();
        const int64_t newId = db_.exec(
            "INSERT INTO memories(kind,text,source,ts) VALUES(?1,?2,'mobile',?3)",
            {kind, text, now});
        json created;
        db_.query("SELECT id,kind,text,source,user_id,ts FROM memories WHERE id=?1",
                  {newId}, [&](const Row& r) { created = jm::memoryFromRow(r); });
        return Response::json(201, created.dump());
    }

    return Response::error(405, "method_not_allowed", "use GET or POST");
}

// ─── /personalities ─────────────────────────────────────────────────────────

Response HttpRouter::routePersonalities(const Request& req, const Parsed& p) {
    const bool active = p.segments.size() >= 4 && p.segments[3] == QLatin1String("active");

    if (active && req.method == QLatin1String("GET")) {
        return Response::json(200,
            json{{"name", bridge_.activePersonality().toStdString()}}.dump());
    }
    if (active && (req.method == QLatin1String("PUT") || req.method == QLatin1String("POST"))) {
        json b; Response err;
        if (!parseBody(req.body, b, err)) return err;
        const std::string name = b.value("name", std::string());
        if (name.empty()) return Response::error(400, "missing_name", "name is required");
        bridge_.setPersonality(QString::fromStdString(name));
        return Response::json(200, json{{"name", name}}.dump());
    }

    if (req.method == QLatin1String("GET")) {
        // Read the personalities table directly so we get voice/wake_phrase/active.
        json arr = json::array();
        db_.query("SELECT name,voice,wake_phrase,is_active FROM personalities ORDER BY name ASC",
                  {}, [&](const Row& r) { arr.push_back(jm::personalityFromRow(r)); });
        return Response::json(200, arr.dump());
    }

    return Response::error(405, "method_not_allowed", "unsupported method for /personalities");
}

// ─── /models ────────────────────────────────────────────────────────────────

Response HttpRouter::routeModels(const Request& req) {
    if (req.method != QLatin1String("GET"))
        return Response::error(405, "method_not_allowed", "use GET");
    // Reuse the bridge (same data the desktop Model Manager shows), normalized to
    // the ModelDTO field names.
    json arr = json::array();
    const QVariantList list = bridge_.models();
    for (const QVariant& v : list) arr.push_back(jm::modelFromVariant(v));
    return Response::json(200, arr.dump());
}

// ─── /settings ──────────────────────────────────────────────────────────────

Response HttpRouter::routeSettings(const Request& req, const Parsed& p) {
    const bool hasKey = p.segments.size() >= 4;
    const QString key = hasKey ? p.segments[3] : QString();

    if (!hasKey && req.method == QLatin1String("GET")) {
        // Return the full settings map (the mobile Settings screen reads it).
        json obj = json::object();
        db_.query("SELECT key,value FROM settings", {},
                  [&](const Row& r) { obj[r.text(0)] = r.text(1); });
        return Response::json(200, obj.dump());
    }

    if (hasKey && req.method == QLatin1String("GET")) {
        const std::string v = db_.getSetting(key.toStdString(), "");
        return Response::json(200, json{{"key", key.toStdString()}, {"value", v}}.dump());
    }

    // PATCH /settings           with body { key, value }
    // PUT   /settings/:key      with body { value }
    if (req.method == QLatin1String("PATCH") || req.method == QLatin1String("PUT")) {
        json b; Response err;
        if (!parseBody(req.body, b, err)) return err;
        std::string k = hasKey ? key.toStdString() : b.value("key", std::string());
        if (k.empty()) return Response::error(400, "missing_key", "key is required");
        if (!b.contains("value")) return Response::error(400, "missing_value", "value is required");
        // value may arrive as string or bool; normalize to "true"/"false" or text.
        std::string v;
        if (b["value"].is_boolean())   v = b["value"].get<bool>() ? "true" : "false";
        else if (b["value"].is_string()) v = b["value"].get<std::string>();
        else                            v = b["value"].dump();

        // Privacy toggles must route through the bridge so they also emit a
        // PrivacyChanged event (and stay consistent with the desktop toggles).
        if (k.rfind("privacy.", 0) == 0) {
            const bool on = (v == "true" || v == "1");
            bridge_.setPrivacy(QString::fromStdString(k), on);
        } else {
            cfg_.set(k.c_str(), v);
        }
        return Response::json(200, json{{"key", k}, {"value", v}}.dump());
    }

    return Response::error(405, "method_not_allowed", "unsupported method for /settings");
}

// ─── /devices ───────────────────────────────────────────────────────────────

Response HttpRouter::routeDevices(const Request& req, const Parsed& p) {
    const bool hasId = p.segments.size() >= 4;
    const QString id = hasId ? p.segments[3] : QString();

    if (!hasId && req.method == QLatin1String("GET")) {
        json arr = json::array();
        for (const DeviceRow& d : auth_.listDevices()) {
            // online-ness isn't tracked precisely here; treat "seen in the last
            // 90s" as online (WsHub could later report true presence).
            const bool online = (jm::nowUnix() - d.last_seen) < 90;
            json j{
                {"device_id",  d.device_id.toStdString()},
                {"name",       d.name.toStdString()},
                {"role",       d.role.toStdString()},
                {"platform",   d.platform.toStdString()},
                {"created_at", d.created_at},
                {"last_seen",  d.last_seen},
                {"online",     online},
            };
            arr.push_back(std::move(j));
        }
        return Response::json(200, arr.dump());
    }

    if (hasId && req.method == QLatin1String("DELETE")) {
        if (!auth_.revokeDevice(id))
            return Response::error(404, "not_found", "device not found");
        return Response::noContent();
    }

    return Response::error(405, "method_not_allowed", "use GET or DELETE");
}

// ─── device fabric (v2) ──────────────────────────────────────────────────────

// GET  /fabric/devices            -> edge-device registry (optional ?kind=)
// GET  /fabric/devices/:id        -> one device
// POST /fabric/devices/announce   -> device self-registration (docs/FABRIC.md §3)
// DELETE /fabric/devices/:id      -> forget a device
Response HttpRouter::routeFabric(const Request& req, const Parsed& p) {
    if (!fabric_) return Response::error(503, "fabric_unavailable", "device fabric not running");
    // p.segments: api, v1, fabric, <sub>, <id-or-action>
    const QString sub = p.segments.size() > 3 ? p.segments[3] : QString();
    if (sub != QLatin1String("devices"))
        return Response::error(404, "not_found", "unknown fabric resource");

    const QString tail = p.segments.size() > 4 ? p.segments[4] : QString();

    if (tail == QLatin1String("announce") && req.method == QLatin1String("POST")) {
        json b; Response err;
        if (!parseBody(req.body, b, err)) return err;
        const std::string id = fabric_->ingestAnnounce(b);
        if (id.empty()) return Response::error(400, "bad_announce", "device_id required");
        return Response::json(201, json{{"device_id", id}}.dump());
    }

    if (tail.isEmpty() && req.method == QLatin1String("GET")) {
        const std::string kind = p.query.value(QStringLiteral("kind")).toStdString();
        json arr = json::array();
        for (const auto& d : fabric_->registry().list(kind))
            arr.push_back(jm::edgeDeviceToJson(d));
        return Response::json(200, arr.dump());
    }

    if (!tail.isEmpty() && req.method == QLatin1String("GET")) {
        auto d = fabric_->registry().get(tail.toStdString());
        if (!d) return Response::error(404, "not_found", "device not found");
        return Response::json(200, jm::edgeDeviceToJson(*d).dump());
    }

    if (!tail.isEmpty() && req.method == QLatin1String("DELETE")) {
        db_.exec("DELETE FROM edge_devices WHERE id=?1", {tail.toStdString()});
        return Response::noContent();
    }

    return Response::error(405, "method_not_allowed", "unsupported method for /fabric/devices");
}

// GET /instruments              -> all instruments + their latest reading
// GET /instruments/:id/read     -> latest reading for one instrument
Response HttpRouter::routeInstruments(const Request& req, const Parsed& p) {
    if (req.method != QLatin1String("GET"))
        return Response::error(405, "method_not_allowed", "use GET");

    const bool hasId = p.segments.size() >= 4;
    const std::string id = hasId ? p.segments[3].toStdString() : std::string();
    const bool readLatest = p.segments.size() >= 5 && p.segments[4] == QLatin1String("read");

    if (hasId && readLatest) {
        json out = json::value_t::null;
        db_.query("SELECT value,unit,in_range,ts FROM measurements WHERE instrument_id=?1 "
                  "ORDER BY ts DESC LIMIT 1", {id}, [&](const Row& r) {
                      out = json{{"instrument_id", id}, {"value", r.dbl(0)},
                                 {"unit", r.text(1)}, {"in_range", r.i64(2) != 0},
                                 {"ts", r.i64(3)}};
                  });
        if (out.is_null()) return Response::error(404, "no_reading", "no reading yet");
        return Response::json(200, out.dump());
    }

    json arr = json::array();
    db_.query("SELECT id,device_id,name,channel,unit,device_class,expected_min,expected_max "
              "FROM instruments ORDER BY device_class, name", {},
              [&](const Row& r) { arr.push_back(jm::instrumentFromRow(r)); });
    return Response::json(200, arr.dump());
}

// GET  /lab/sessions        -> recent sessions
// GET  /lab/sessions/:id     -> one session + its steps
// POST /lab/sessions         -> create a session { title, objective? }
Response HttpRouter::routeLab(const Request& req, const Parsed& p) {
    // p.segments: api, v1, lab, sessions, :id
    const QString sub = p.segments.size() > 3 ? p.segments[3] : QString();
    if (sub != QLatin1String("sessions"))
        return Response::error(404, "not_found", "unknown lab resource");
    const bool hasId = p.segments.size() >= 5;
    const int64_t id = hasId ? p.segments[4].toLongLong() : 0;

    if (!hasId && req.method == QLatin1String("GET")) {
        json arr = json::array();
        db_.query("SELECT id,title,objective,status,report_doc_id,started_at,ended_at "
                  "FROM lab_sessions ORDER BY started_at DESC LIMIT 200", {},
                  [&](const Row& r) { arr.push_back(jm::labSessionFromRow(r)); });
        return Response::json(200, arr.dump());
    }

    if (!hasId && req.method == QLatin1String("POST")) {
        json b; Response err;
        if (!parseBody(req.body, b, err)) return err;
        const std::string title = b.value("title", std::string());
        if (title.empty()) return Response::error(400, "missing_title", "title is required");
        const std::string objective = b.value("objective", std::string());
        const int64_t now = jm::nowUnix();
        const int64_t newId = db_.exec(
            "INSERT INTO lab_sessions(title,objective,status,started_at) "
            "VALUES(?1,?2,'active',?3)", {title, objective, now});
        json created;
        db_.query("SELECT id,title,objective,status,report_doc_id,started_at,ended_at "
                  "FROM lab_sessions WHERE id=?1", {newId},
                  [&](const Row& r) { created = jm::labSessionFromRow(r); });
        return Response::json(201, created.dump());
    }

    if (hasId && req.method == QLatin1String("GET")) {
        json session = json::value_t::null;
        db_.query("SELECT id,title,objective,status,report_doc_id,started_at,ended_at "
                  "FROM lab_sessions WHERE id=?1", {id},
                  [&](const Row& r) { session = jm::labSessionFromRow(r); });
        if (session.is_null()) return Response::error(404, "not_found", "session not found");
        json steps = json::array();
        db_.query("SELECT step_no,prompt,expected_kind,expected_unit,measured_value,"
                  "measured_unit,verified,verified_at FROM lab_session_steps "
                  "WHERE session_id=?1 ORDER BY step_no ASC", {id},
                  [&](const Row& r) {
                      steps.push_back(json{
                          {"step_no", r.i64(0)}, {"prompt", r.text(1)},
                          {"expected_kind", r.text(2)}, {"expected_unit", r.text(3)},
                          {"measured_value", r.isNull(4) ? json(nullptr) : json(r.dbl(4))},
                          {"measured_unit", r.text(5)}, {"verified", r.i64(6) != 0},
                          {"verified_at", r.isNull(7) ? json(nullptr) : json(r.i64(7))}});
                  });
        session["steps"] = steps;
        return Response::json(200, session.dump());
    }

    return Response::error(405, "method_not_allowed", "unsupported method for /lab/sessions");
}

// POST /cameras/:id/events  -> edge camera detection + clip metadata (FABRIC.md §4)
Response HttpRouter::routeCameraEvents(const Request& req, int cameraId) {
    if (!fabric_) return Response::error(503, "fabric_unavailable", "device fabric not running");
    if (req.method != QLatin1String("POST"))
        return Response::error(405, "method_not_allowed", "use POST");
    json b; Response err;
    if (!parseBody(req.body, b, err)) return err;
    const int64_t eventId = fabric_->ingestCameraEvent(cameraId, b);
    if (eventId < 0) return Response::error(400, "bad_event", "could not record event");
    return Response::json(201, json{{"event_id", eventId}}.dump());
}

// POST /cameras/:id/frame  -> raw JPEG body, pushed as a live UI tile.
Response HttpRouter::routeCameraFrame(const Request& req, int cameraId) {
    if (!fabric_) return Response::error(503, "fabric_unavailable", "device fabric not running");
    if (req.method != QLatin1String("POST"))
        return Response::error(405, "method_not_allowed", "use POST");
    if (req.body.isEmpty()) return Response::error(400, "empty_body", "JPEG body required");
    fabric_->ingestFrame(cameraId, req.body);
    return Response::noContent();
}

// ─── media file serving ─────────────────────────────────────────────────────

Response HttpRouter::serveJpegFile(const std::string& absPath) {
    // thumb_path may be stored relative to the media dir; resolve both ways.
    std::filesystem::path path(absPath);
    if (path.is_relative())
        path = Paths::instance().media() / path;

    QFile f(QString::fromStdString(path.string()));
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return Response::error(404, "not_found", "image not found");
    QByteArray data = f.readAll();
    f.close();

    const char* ct = "image/jpeg";
    const QString lower = QFileInfo(f).suffix().toLower();
    if (lower == QLatin1String("png")) ct = "image/png";
    Response r = Response::bytes(200, ct, std::move(data));
    r.headers["cache-control"] = "private, max-age=30";
    return r;
}

} // namespace polymath
