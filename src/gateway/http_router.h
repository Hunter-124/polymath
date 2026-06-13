#pragma once
//
// HttpRouter — the transport-agnostic request handler.
//
// A SINGLE entry point, handle(method, path, headers, body), implements every
// REST endpoint in app/src/api/contract.ts (the ENDPOINTS map).  It is
// deliberately decoupled from any socket: BOTH the local QHttpServer
// (http_server.cpp) AND the relay `req` frames (relay_client.cpp) feed requests
// through this same function, so LAN and remote behave identically.
//
// Dependencies:
//   * Database + Config  — reads/writes the canonical tables (schema.h).
//   * IAssistantBridge   — actions that must touch the live app (chat, privacy,
//                          find-object, personality switch, model list).
//   * Auth               — pairing + token verification + device CRUD.
//
// Auth policy: every route requires a valid Bearer token EXCEPT POST /pair and
// GET /status.  For GET media routes (snapshots/thumbnails/stream) a `?token=`
// query parameter is also accepted, because <img>/<video> tags can't set an
// Authorization header.
//
#include <QByteArray>
#include <QHash>
#include <QString>
#include <map>
#include <string>
#include <vector>

namespace polymath {

class Database;
class Config;
class Auth;
class IAssistantBridge;
class FabricService;

// A buffered request, normalized across transports.  Header keys are stored
// lower-cased so lookups are case-insensitive.
struct Request {
    QString                            method;   // "GET", "POST", ...
    QString                            path;     // full path incl. query, e.g. "/api/v1/status?x=1"
    std::map<std::string, std::string> headers;  // lower-cased keys
    QByteArray                         body;     // raw bytes
};

// A buffered response, transport-agnostic (the server/relay serialize it).
struct Response {
    int                                status = 200;
    std::map<std::string, std::string> headers;       // e.g. {"content-type","application/json"}
    QByteArray                         body;

    // Convenience builders used throughout the router.
    static Response json(int status, const std::string& jsonBody);
    static Response text(int status, const std::string& msg);
    static Response bytes(int status, const char* contentType, QByteArray data);
    static Response error(int status, const std::string& code, const std::string& message);
    static Response noContent();   // 204
};

class HttpRouter {
public:
    // `fabric` is optional: when non-null, the device-fabric routes (camera event
    // ingest, /fabric/devices, /instruments, /lab) are served; otherwise they 503.
    HttpRouter(IAssistantBridge& bridge, Database& db, Config& cfg, Auth& auth,
               FabricService* fabric = nullptr);

    // THE entry point.  Thread-safe to the extent the DB (WAL + mutex) and the
    // bridge (which marshals to workers) are; multiple transports may call it
    // concurrently.
    Response handle(const QString& method,
                    const QString& path,
                    const std::map<std::string, std::string>& headers,
                    const QByteArray& body);

    // Records process start so /status can report uptime_s.
    void setStartTime(int64_t unixSeconds) { start_unix_ = unixSeconds; }

private:
    // --- routing helpers -------------------------------------------------
    // Splits "/api/v1/foo/bar?x=1" -> path segments ["api","v1","foo","bar"] and
    // a parsed query map.
    struct Parsed {
        std::vector<QString>               segments;
        QHash<QString, QString>            query;
    };
    static Parsed parsePath(const QString& path);

    // Pull a bearer token from the Authorization header OR the ?token= query.
    static QString extractToken(const std::map<std::string, std::string>& headers,
                                const QHash<QString, QString>& query);

    // --- endpoint groups (each returns a fully-formed Response) ----------
    Response routePair(const Request& req);
    Response routeMe(const Request& req, const QString& deviceId, const QString& role);
    Response routeStatus(const Request& req);

    Response routeChat(const Request& req, const Parsed& p);
    Response routeChatHistory(const Request& req, const Parsed& p);

    Response routeCameras(const Request& req, const Parsed& p, const QString& token);
    Response routeCameraMedia(const Request& req, int cameraId, const QString& kind);
    Response routeFindObject(const Request& req);

    Response routeTasks(const Request& req, const Parsed& p);
    Response routeReminders(const Request& req, const Parsed& p);
    Response routeShopping(const Request& req, const Parsed& p);

    Response routeTimeline(const Request& req, const Parsed& p);
    Response routeTimelineThumb(const Request& req, int eventId);
    Response routeMemory(const Request& req, const Parsed& p);

    Response routePersonalities(const Request& req, const Parsed& p);
    Response routeModels(const Request& req);

    Response routeSettings(const Request& req, const Parsed& p);
    Response routeDevices(const Request& req, const Parsed& p);

    // --- device fabric (v2) ----------------------------------------------
    Response routeFabric(const Request& req, const Parsed& p);       // /fabric/devices[/announce]
    Response routeInstruments(const Request& req, const Parsed& p);  // /instruments[/:id[/read]]
    Response routeLab(const Request& req, const Parsed& p);          // /lab/sessions[/:id]
    Response routeCameraEvents(const Request& req, int cameraId);    // POST /cameras/:id/events
    Response routeCameraFrame(const Request& req, int cameraId);     // POST /cameras/:id/frame

    // Reads a JPEG file from disk into a Response (404 if missing).
    Response serveJpegFile(const std::string& absPath);

    IAssistantBridge& bridge_;
    Database&         db_;
    Config&           cfg_;
    Auth&             auth_;
    FabricService*    fabric_ = nullptr;
    int64_t           start_unix_ = 0;
};

} // namespace polymath
