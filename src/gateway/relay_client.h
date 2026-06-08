#pragma once
//
// RelayClient — the home side of the reverse tunnel (the "agent").
//
// When gateway.remote_enabled is true, this dials an OUTBOUND WebSocket to
// `gateway.relay_url + "/agent"`, authenticates with {t:'hello',home_id,secret},
// and then services the relay's framed-JSON protocol so phones reach us off-LAN
// without any inbound port.  It mirrors cloud/relay/src/protocol.ts (the source
// of truth) and cloud/relay/src/tunnel.ts (the relay-side peer):
//
//   relay → us:  hello_ok, req, ws_open, ws_msg, ws_close, ping, pong
//   us → relay:  hello, res, ws_open_ok, ws_err, ws_msg, ws_close, ping, pong
//
//   * `req`      → run HttpRouter::handle, reply with `res` (binary-safe base64).
//   * `ws_open`  → register a logical client in WsHub (via a RelayChannel that
//                  emits ws_msg back over the tunnel); reply ws_open_ok / ws_err.
//   * `ws_msg`   → deliver the client's control frame to WsHub.
//   * `ws_close` → drop the logical client.
//   * ping/pong  → app-level keepalive (we answer ping with pong, and send our
//                  own ping on an interval so a wedged relay is detected).
//
// Auto-reconnect with exponential backoff keeps the tunnel up across relay
// restarts and flaky links.  Toggling remote access calls setEnabled().
//
#include <nlohmann/json.hpp>

#include <QHash>
#include <QObject>
#include <QString>
#include <memory>

class QWebSocket;
class QTimer;

namespace polymath {

class HttpRouter;
class WsHub;
class Database;
class RelayChannel;   // defined in the .cpp (logical client over the tunnel)

class RelayClient : public QObject {
    Q_OBJECT
public:
    // Routes `req` frames into `router`; bridges `ws_*` into `hub`; reads
    // home_id/secret/relay_url/remote_enabled from `db` (gateway.* settings).
    RelayClient(HttpRouter& router, WsHub& hub, Database& db, QObject* parent = nullptr);
    ~RelayClient() override;

    // Start honoring gateway.remote_enabled.  If enabled, dials immediately.
    void start();
    void stop();

    // Flip remote access at runtime (the desktop "Allow remote access" toggle).
    // Persists nothing itself — the caller updates the setting; this just opens
    // or closes the tunnel.
    void setEnabled(bool enabled);

    bool connected() const;

signals:
    void connectedChanged(bool connected);

private:
    // Connection lifecycle.
    void connectNow();
    void scheduleReconnect();
    void onConnected();
    void onDisconnected();
    void onTextFrame(const QString& text);

    // Protocol handlers.
    void handleReq(const nlohmann::json& msg);
    void handleWsOpen(const nlohmann::json& msg);
    void handleWsMsg(const nlohmann::json& msg);
    void handleWsClose(const nlohmann::json& msg);

    // Send one framed-JSON message over the tunnel (no-op if not open).
    void send(const nlohmann::json& msg);

    // Called by a RelayChannel to push a server→client frame (ws_msg) or to
    // signal the upstream is closing (ws_close).
    friend class RelayChannel;
    void sendChannelText(const QString& cid, const QString& text);
    void sendChannelClose(const QString& cid);

    HttpRouter& router_;
    WsHub&      hub_;
    Database&   db_;

    std::unique_ptr<QWebSocket> ws_;
    QTimer*                     reconnect_timer_ = nullptr;
    QTimer*                     ping_timer_      = nullptr;
    int                         backoff_ms_      = 0;
    bool                        enabled_         = false;
    bool                        hello_ok_        = false;

    // cid -> logical client channel currently bridged through WsHub.
    QHash<QString, std::shared_ptr<RelayChannel>> channels_;
};

} // namespace polymath
