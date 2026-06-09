#pragma once
//
// HttpServer — the LAN-facing transport.
//
// Binds a QTcpServer on 0.0.0.0:<port> and serves it via QHttpServer, forwarding
// every REST request into HttpRouter::handle (the same function the relay uses).
// The single special case is GET /api/v1/events, which is a WebSocket upgrade: a
// QWebSocketServer in NonSecure mode accepts the socket and hands it to WsHub.
//
// We run one QWebSocketServer alongside the HTTP server and a QTcpServer that
// peeks each incoming connection: an Upgrade: websocket request to the events
// path is routed to the WS server; everything else goes to QHttpServer.  In
// practice Qt 6.4+ lets QHttpServer and QWebSocketServer share a QTcpServer via
// QHttpServer::bind / the websocket upgrade signal — see the .cpp for the exact
// wiring and the version note.
//
#include <QHttpServerResponse>   // returned by-value from handleRest()
#include <QObject>
#include <memory>

class QHttpServer;
class QHttpServerRequest;
class QTcpServer;
class QWebSocketServer;

namespace polymath {

class HttpRouter;
class WsHub;

class HttpServer : public QObject {
    Q_OBJECT
public:
    HttpServer(HttpRouter& router, WsHub& hub, QObject* parent = nullptr);
    ~HttpServer() override;

    // Bind and start listening on 0.0.0.0:<port>.  Returns false if the bind
    // fails (e.g. port in use).
    bool listen(quint16 port);
    void stop();

    quint16 port() const { return port_; }

private:
    void installRoutes();   // wire QHttpServer -> HttpRouter::handle
    void installWebSocketUpgrade();
    // Forward one parsed HTTP request through the shared router and adapt the
    // Response back to QHttpServer's type.
    QHttpServerResponse handleRest(const QHttpServerRequest& req);

    HttpRouter& router_;
    WsHub&      hub_;

    std::unique_ptr<QHttpServer>      http_;
    std::unique_ptr<QTcpServer>       tcp_;
    std::unique_ptr<QWebSocketServer> wss_;
    quint16                           port_ = 0;
};

} // namespace polymath
