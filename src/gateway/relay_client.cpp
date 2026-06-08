#include "relay_client.h"

#include "gateway_db.h"
#include "http_router.h"
#include "logging.h"
#include "ws_hub.h"

#include <QTimer>
#include <QUrl>
#include <QWebSocket>

#include <algorithm>
#include <cctype>

namespace polymath {

using nlohmann::json;

// Keepalive + backoff tuning (mirrors the relay's defaults in config.ts).
static constexpr int kPingIntervalMs   = 20'000;
static constexpr int kBackoffStartMs   = 1'000;
static constexpr int kBackoffMaxMs     = 30'000;

// ─── RelayChannel ────────────────────────────────────────────────────────────
//
// A logical client living entirely inside the tunnel.  WsHub treats it like any
// other IClientChannel; sendText() turns into a `ws_msg` frame back to the relay
// (which forwards it to the phone), and close() into a `ws_close`.

class RelayChannel : public IClientChannel {
public:
    RelayChannel(RelayClient* owner, QString cid) : owner_(owner), cid_(std::move(cid)) {}
    void sendText(const QString& text) override {
        if (owner_) owner_->sendChannelText(cid_, text);
    }
    void close() override {
        if (owner_) owner_->sendChannelClose(cid_);
    }
    QString id() const override { return cid_; }
private:
    RelayClient* owner_;   // outlives the channel (owns it via channels_)
    QString      cid_;
};

// ─── construction ────────────────────────────────────────────────────────────

RelayClient::RelayClient(HttpRouter& router, WsHub& hub, Database& db, QObject* parent)
    : QObject(parent), router_(router), hub_(hub), db_(db) {
    reconnect_timer_ = new QTimer(this);
    reconnect_timer_->setSingleShot(true);
    connect(reconnect_timer_, &QTimer::timeout, this, &RelayClient::connectNow);

    ping_timer_ = new QTimer(this);
    ping_timer_->setInterval(kPingIntervalMs);
    connect(ping_timer_, &QTimer::timeout, this, [this] {
        if (hello_ok_) send(json{{"t", "ping"}});
    });
}

RelayClient::~RelayClient() { stop(); }

// ─── lifecycle ───────────────────────────────────────────────────────────────

void RelayClient::start() {
    enabled_ = GatewayDb::remoteEnabled(db_);
    if (enabled_) connectNow();
}

void RelayClient::stop() {
    enabled_ = false;
    if (reconnect_timer_) reconnect_timer_->stop();
    if (ping_timer_)      ping_timer_->stop();
    // Tear down any bridged clients first so the hub stops pushing to them.
    for (auto it = channels_.begin(); it != channels_.end(); ++it)
        hub_.removeChannel(it.key());
    channels_.clear();
    if (ws_) {
        ws_->close();
        ws_.reset();
    }
    hello_ok_ = false;
}

void RelayClient::setEnabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;
    if (enabled_) {
        backoff_ms_ = 0;
        connectNow();
    } else {
        stop();
    }
}

bool RelayClient::connected() const { return hello_ok_; }

void RelayClient::connectNow() {
    if (!enabled_) return;

    const std::string base = GatewayDb::relayUrl(db_);
    if (base.empty()) {
        PM_WARN("gateway/relay: remote enabled but gateway.relay_url is empty");
        return;   // nothing to dial; user must set a relay url
    }

    // Build "<relay>/agent".
    QString url = QString::fromStdString(base);
    if (url.endsWith('/')) url.chop(1);
    url += QStringLiteral("/agent");

    ws_ = std::make_unique<QWebSocket>();
    connect(ws_.get(), &QWebSocket::connected,    this, &RelayClient::onConnected);
    connect(ws_.get(), &QWebSocket::disconnected, this, &RelayClient::onDisconnected);
    connect(ws_.get(), &QWebSocket::textMessageReceived, this, &RelayClient::onTextFrame);
    connect(ws_.get(),
            QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
            this, [this](QAbstractSocket::SocketError) {
                PM_WARN("gateway/relay: socket error: {}",
                        ws_ ? ws_->errorString().toStdString() : "?");
                // disconnected() follows and drives the reconnect.
            });

    PM_INFO("gateway/relay: dialing {}", url.toStdString());
    ws_->open(QUrl(url));
}

void RelayClient::scheduleReconnect() {
    if (!enabled_) return;
    backoff_ms_ = backoff_ms_ == 0 ? kBackoffStartMs
                                   : std::min(backoff_ms_ * 2, kBackoffMaxMs);
    PM_INFO("gateway/relay: reconnecting in {} ms", backoff_ms_);
    reconnect_timer_->start(backoff_ms_);
}

void RelayClient::onConnected() {
    // Announce ourselves; the relay validates home_id+secret (registry.ts).
    send(json{
        {"t",       "hello"},
        {"home_id", GatewayDb::homeId(db_)},
        {"secret",  GatewayDb::secret(db_)},
        {"agent",   "polymath-gateway/1"},
    });
    // hello_ok arrives as a frame; we mark connected then.  Start pinging now so
    // a relay that never acks still gets liveness traffic.
    ping_timer_->start();
}

void RelayClient::onDisconnected() {
    const bool wasUp = hello_ok_;
    hello_ok_ = false;
    ping_timer_->stop();

    // Close every bridged client (their phones see the WS drop and reconnect).
    for (auto it = channels_.begin(); it != channels_.end(); ++it)
        hub_.removeChannel(it.key());
    channels_.clear();

    if (wasUp) emit connectedChanged(false);
    PM_INFO("gateway/relay: disconnected");
    scheduleReconnect();
}

void RelayClient::onTextFrame(const QString& text) {
    json msg;
    try {
        msg = json::parse(text.toStdString());
    } catch (...) {
        PM_WARN("gateway/relay: unparseable frame");
        return;
    }
    const std::string t = msg.value("t", std::string());

    if (t == "hello_ok") {
        hello_ok_ = true;
        backoff_ms_ = 0;   // healthy connection resets backoff
        PM_INFO("gateway/relay: tunnel established (home_id={})", GatewayDb::homeId(db_));
        emit connectedChanged(true);
        return;
    }
    if (t == "ping") { send(json{{"t", "pong"}}); return; }
    if (t == "pong") { return; }   // liveness only
    if (t == "req")       { handleReq(msg);     return; }
    if (t == "ws_open")   { handleWsOpen(msg);  return; }
    if (t == "ws_msg")    { handleWsMsg(msg);   return; }
    if (t == "ws_close")  { handleWsClose(msg); return; }

    PM_WARN("gateway/relay: unknown frame type '{}'", t);
}

// ─── REST proxying ───────────────────────────────────────────────────────────

void RelayClient::handleReq(const json& msg) {
    const std::string id     = msg.value("id", std::string());
    const std::string method = msg.value("method", std::string("GET"));
    const std::string path   = msg.value("path", std::string());

    // Flatten headers (lower-cased for the router's case-insensitive lookups).
    std::map<std::string, std::string> headers;
    if (msg.contains("headers") && msg["headers"].is_object()) {
        for (auto it = msg["headers"].begin(); it != msg["headers"].end(); ++it) {
            std::string k = it.key();
            std::transform(k.begin(), k.end(), k.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (it.value().is_string()) headers[k] = it.value().get<std::string>();
        }
    }

    // Decode the base64 body (null/absent => empty).
    QByteArray body;
    if (msg.contains("body") && msg["body"].is_string())
        body = QByteArray::fromBase64(
            QByteArray::fromStdString(msg["body"].get<std::string>()));

    const Response r = router_.handle(QString::fromStdString(method),
                                      QString::fromStdString(path),
                                      headers, body);

    // Build the res frame.  Body is base64 (binary-safe for JPEG etc.); null when
    // empty to match the protocol.
    json headersOut = json::object();
    for (const auto& [k, v] : r.headers) headersOut[k] = v;

    json res{
        {"t",       "res"},
        {"id",      id},
        {"status",  r.status},
        {"headers", std::move(headersOut)},
    };
    if (r.body.isEmpty()) res["body"] = json::value_t::null;
    else                  res["body"] = r.body.toBase64().toStdString();
    send(res);
}

// ─── WebSocket bridging ──────────────────────────────────────────────────────

void RelayClient::handleWsOpen(const json& msg) {
    const std::string cid   = msg.value("cid", std::string());
    const std::string path  = msg.value("path", std::string());
    const std::string query = msg.value("query", std::string());
    if (cid.empty()) return;

    const QString qcid = QString::fromStdString(cid);

    // We only bridge the events stream.
    if (path != "/api/v1/events") {
        send(json{{"t", "ws_err"}, {"cid", cid}, {"error", "unknown_ws_path"}});
        return;
    }

    auto channel = std::make_shared<RelayChannel>(this, qcid);
    // WsHub verifies the ?token= (carried in `query`) before accepting.
    if (!hub_.addChannel(channel, QString::fromStdString(query))) {
        send(json{{"t", "ws_err"}, {"cid", cid}, {"error", "unauthorized"}});
        return;
    }
    channels_.insert(qcid, std::move(channel));
    send(json{{"t", "ws_open_ok"}, {"cid", cid}});
}

void RelayClient::handleWsMsg(const json& msg) {
    const std::string cid  = msg.value("cid", std::string());
    const std::string data = msg.value("data", std::string());
    if (cid.empty()) return;
    const QString qcid = QString::fromStdString(cid);
    if (!channels_.contains(qcid)) return;
    // Client → server control message (subscribe/unsubscribe/ping).
    hub_.feedChannel(qcid, QString::fromStdString(data));
}

void RelayClient::handleWsClose(const json& msg) {
    const std::string cid = msg.value("cid", std::string());
    if (cid.empty()) return;
    const QString qcid = QString::fromStdString(cid);
    if (channels_.remove(qcid) > 0)
        hub_.removeChannel(qcid);
}

// ─── outbound helpers ────────────────────────────────────────────────────────

void RelayClient::send(const json& msg) {
    if (!ws_ || ws_->state() != QAbstractSocket::ConnectedState) return;
    ws_->sendTextMessage(QString::fromStdString(msg.dump()));
}

void RelayClient::sendChannelText(const QString& cid, const QString& text) {
    send(json{{"t", "ws_msg"}, {"cid", cid.toStdString()}, {"data", text.toStdString()}});
}

void RelayClient::sendChannelClose(const QString& cid) {
    if (channels_.remove(cid) > 0)
        send(json{{"t", "ws_close"}, {"cid", cid.toStdString()}});
}

} // namespace polymath
