#include "http_server.h"

#include "http_router.h"
#include "logging.h"
#include "ws_hub.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#  include <QHttpHeaders>
#endif
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketServer>

#include <map>

namespace polymath {

// Translate a std::map header bag into a QHttpServerResponse's header set and an
// HttpRouter::Response into a QHttpServerResponse.
static QHttpServerResponse toHttpResponse(const Response& r) {
    QHttpServerResponse resp(
        QByteArray::fromStdString(r.headers.count("content-type")
                                      ? r.headers.at("content-type")
                                      : std::string("application/octet-stream")),
        r.body,
        static_cast<QHttpServerResponse::StatusCode>(r.status));
    // Attach any remaining headers (cache-control, www-authenticate, ...).
    // QHttpServerResponse::setHeader was replaced by the QHttpHeaders-based
    // headers() accessor in Qt 6.8; guard both.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QHttpHeaders extra = resp.headers();
    for (const auto& [k, v] : r.headers) {
        if (k == "content-type") continue;
        extra.append(QByteArray::fromStdString(k), QByteArray::fromStdString(v));
    }
    resp.setHeaders(std::move(extra));
#else
    for (const auto& [k, v] : r.headers) {
        if (k == "content-type") continue;
        resp.setHeader(QByteArray::fromStdString(k), QByteArray::fromStdString(v));
    }
#endif
    return resp;
}

// Flatten a QHttpServerRequest's headers into the lower-cased map the router
// expects.
static std::map<std::string, std::string> flattenHeaders(const QHttpServerRequest& req) {
    std::map<std::string, std::string> out;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    const auto headers = req.headers();
    for (const auto& field : headers.fields()) {
        out[field.name.toString().toLower().toStdString()] =
            field.value.toString().toStdString();
    }
#else
    // Qt 6.4–6.6: headers() returns a QList<QPair<QByteArray,QByteArray>>.
    const auto headers = req.headers();
    for (const auto& kv : headers) {
        out[QString::fromUtf8(kv.first).toLower().toStdString()] =
            QString::fromUtf8(kv.second).toStdString();
    }
#endif
    return out;
}

// ─── construction ───────────────────────────────────────────────────────────

HttpServer::HttpServer(HttpRouter& router, WsHub& hub, QObject* parent)
    : QObject(parent), router_(router), hub_(hub) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::listen(quint16 port) {
    http_ = std::make_unique<QHttpServer>(this);
    tcp_  = std::make_unique<QTcpServer>(this);
    wss_  = std::make_unique<QWebSocketServer>(
        QStringLiteral("polymath-gateway"), QWebSocketServer::NonSecureMode, this);

    installRoutes();
    installWebSocketUpgrade();

    if (!tcp_->listen(QHostAddress::Any, port)) {
        PM_ERROR("gateway/http: failed to bind 0.0.0.0:{} ({})",
                 port, tcp_->errorString().toStdString());
        return false;
    }
    // Hand the shared TCP server to QHttpServer.  When a connection's first
    // request is a WebSocket upgrade to /events, the QWebSocketServer adopts it
    // instead (see installWebSocketUpgrade()).
    if (!http_->bind(tcp_.get())) {
        PM_ERROR("gateway/http: QHttpServer failed to bind the tcp server");
        return false;
    }
    port_ = tcp_->serverPort();
    PM_INFO("gateway/http: listening on 0.0.0.0:{}", port_);
    return true;
}

void HttpServer::stop() {
    if (tcp_)  tcp_->close();
    if (wss_)  wss_->close();
    http_.reset();
    wss_.reset();
    tcp_.reset();
    port_ = 0;
}

// ─── REST routing ───────────────────────────────────────────────────────────

void HttpServer::installRoutes() {
    // We don't enumerate routes in QHttpServer at all: the endpoint map lives in
    // ONE place (HttpRouter, shared with the relay).  Instead we install a
    // missing-handler so EVERY request that isn't a WebSocket upgrade funnels
    // through HttpRouter::handle, regardless of path depth or method.  This keeps
    // QHttpServer's matcher out of the contract entirely.
    //
    // A trivial root route answers health probes (and stops QHttpServer warning
    // about a server with no routes on some versions).
    http_->route("/", [](const QHttpServerRequest&) {
        return QHttpServerResponse(QByteArrayLiteral("text/plain"),
                                   QByteArrayLiteral("Polymath gateway"),
                                   QHttpServerResponse::StatusCode::Ok);
    });

    // The missing-handler signature gained a `context` object (and switched the
    // responder to a reference) in Qt 6.5; 6.4 took just the handler with a
    // QHttpServerResponder&& rvalue.  Guard both.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    http_->setMissingHandler(
        this,
        [this](const QHttpServerRequest& req, QHttpServerResponder& responder) {
            responder.sendResponse(handleRest(req));
        });
#else
    http_->setMissingHandler(
        [this](const QHttpServerRequest& req, QHttpServerResponder&& responder) {
            responder.sendResponse(handleRest(req));
        });
#endif
}

// Forward one QHttpServerRequest through the shared router.
QHttpServerResponse HttpServer::handleRest(const QHttpServerRequest& req) {
    // Full path INCLUDING query so the router can read ?token=, ?limit=, etc.
    const QUrl url = req.url();
    QString fullPath = url.path();
    if (!url.query(QUrl::FullyEncoded).isEmpty())
        fullPath += QLatin1Char('?') + url.query(QUrl::FullyEncoded);

    QString method;
    switch (req.method()) {
        case QHttpServerRequest::Method::Get:    method = QStringLiteral("GET"); break;
        case QHttpServerRequest::Method::Post:   method = QStringLiteral("POST"); break;
        case QHttpServerRequest::Method::Put:    method = QStringLiteral("PUT"); break;
        case QHttpServerRequest::Method::Patch:  method = QStringLiteral("PATCH"); break;
        case QHttpServerRequest::Method::Delete: method = QStringLiteral("DELETE"); break;
        default:                                 method = QStringLiteral("GET"); break;
    }

    const Response r = router_.handle(method, fullPath, flattenHeaders(req), req.body());
    return toHttpResponse(r);
}

// ─── WebSocket upgrade for /api/v1/events ────────────────────────────────────

void HttpServer::installWebSocketUpgrade() {
    // QHttpServer (6.4+) emits a signal when an incoming request asks to upgrade
    // to WebSocket; we accept it on our QWebSocketServer and pass the resulting
    // QWebSocket to the hub.  The events path carries ?token= for auth.
    //
    // NOTE: The exact upgrade-signal API moved across Qt minor versions
    // (newWebSocketConnection on QHttpServer in 6.4–6.5; a missingHandler /
    // WebSocketUpgradeVerdict flow in 6.8).  We connect via QWebSocketServer's
    // newConnection, which QHttpServer feeds when bound — the version-specific
    // glue lives behind the macro below.

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // 6.8+: register an upgrade verdict so QHttpServer routes /events upgrades to
    // our QWebSocketServer.
    http_->addWebSocketUpgradeVerdict(
        [](const QHttpServerRequest& req) {
            return req.url().path() == QLatin1String("/api/v1/events")
                       ? QHttpServerWebSocketUpgradeResponse::accept()
                       : QHttpServerWebSocketUpgradeResponse::passToServer();
        });
#endif

    // QHttpServer forwards accepted upgrades to any QWebSocketServer associated
    // with the same tcp server; in 6.4–6.7 we associate by handling the
    // newWebSocketConnection signal it emits.  Both paths land here:
    connect(http_.get(), &QHttpServer::newWebSocketConnection, this, [this]() {
        while (http_->hasPendingWebSocketConnections()) {
            QWebSocket* sock = http_->nextPendingWebSocketConnection().release();
            if (!sock) break;
            const QString path = sock->requestUrl().path();
            if (path != QLatin1String("/api/v1/events")) {
                sock->close(QWebSocketProtocol::CloseCodePolicyViolated, "unknown ws path");
                sock->deleteLater();
                continue;
            }
            // Token rides in the query string (?token=...).
            hub_.addSocket(sock, sock->requestUrl().query());
        }
    });
}

} // namespace polymath
