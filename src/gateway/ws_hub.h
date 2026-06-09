#pragma once
//
// WsHub — the fan-out hub for the `/api/v1/events` WebSocket stream.
//
// It owns the set of connected clients and bridges EventBus signals to them as
// ServerEvent envelopes (json_map).  A "client" is anything implementing
// IClientChannel, which lets us treat two very different sources uniformly:
//
//   * a REAL QWebSocket accepted by the local server (http_server.cpp), and
//   * a LOGICAL channel tunnelled from the relay (relay_client.cpp), where the
//     phone's socket lives on the relay and we only see ws_msg frames.
//
// Per-client state: a topic filter (which ServerEventType's it wants) and a
// camera filter (which camera_ids' frames it wants).  Clients send control
// messages — {type:'subscribe'|'unsubscribe'|'ping', topics?, camera_ids?} — to
// adjust these (contract.ts ClientCommand).
//
// THREADING: WsHub lives on the gateway thread.  It connects to EventBus signals
// with the default (auto/queued) connection type, so signals emitted from other
// worker threads are delivered on the gateway thread — no locking needed for the
// client map as long as every mutation happens on that thread.  Real sockets are
// created on the gateway thread; relay channels call addChannel/feedChannel via
// queued invocations (relay_client marshals onto this thread).
//
#include "event_bus.h"

#include <nlohmann/json.hpp>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <memory>

class QWebSocket;

namespace polymath {

class Auth;

// Abstract sink for one client connection.  The hub writes text frames here and
// is told when the client goes away.
class IClientChannel {
public:
    virtual ~IClientChannel() = default;
    // Send one text frame (a serialized ServerEvent or control reply).
    virtual void sendText(const QString& text) = 0;
    // Close the channel (best-effort).
    virtual void close() = 0;
    // Stable id for logging/bookkeeping.
    virtual QString id() const = 0;
};

class WsHub : public QObject {
    Q_OBJECT
public:
    // `auth` validates the per-client token (from ?token=) before a client is
    // accepted.  The hub subscribes to EventBus in the constructor.
    explicit WsHub(Auth& auth, QObject* parent = nullptr);
    ~WsHub() override;

    // --- real local sockets ----------------------------------------------
    // Adopt a freshly-upgraded QWebSocket from the local server.  `query` is the
    // upgrade request's query string (carries ?token=).  Verifies the token;
    // closes the socket and returns false if it's missing/invalid.  On success
    // the hub owns the socket (parents it) and wires its signals.
    bool addSocket(QWebSocket* socket, const QString& query);

    // --- relay-bridged logical channels ----------------------------------
    // Register a logical channel (the relay opened a client WS for us).  `query`
    // is the upgrade query string (token).  Returns false if auth fails (the
    // caller should reply ws_err to the relay).  Takes ownership of `channel`.
    bool addChannel(std::shared_ptr<IClientChannel> channel, const QString& query);
    // Deliver an inbound control message from a relay-bridged client.
    void feedChannel(const QString& channelId, const QString& text);
    // The relay told us the client closed; drop the channel.
    void removeChannel(const QString& channelId);

    int clientCount() const { return clients_.size(); }

signals:
    // Emitted when the live client count changes (so GatewayService/UI can show
    // "N devices connected").
    void clientCountChanged(int count);

private:
    // Per-connection bookkeeping.
    struct Client {
        std::shared_ptr<IClientChannel> channel;     // sink (real socket or relay)
        QSet<QString>                   topics;       // empty => all topics
        QSet<int>                       cameras;      // empty => no frames (opt-in)
        QString                         deviceId;
    };

    // --- EventBus subscription -------------------------------------------
    void connectBus();
    // Build an envelope and push to every client subscribed to `type`.
    void broadcast(const char* type, nlohmann::json data);
    // Frames are special: only clients that opted into the specific camera_id.
    void broadcastFrame(const Frame& f);

    // --- control protocol -------------------------------------------------
    // Handle an inbound {type:'subscribe'|...} message for a client id.
    void handleControl(const QString& clientId, const QString& text);

    // --- client lifecycle -------------------------------------------------
    QString registerClient(std::shared_ptr<IClientChannel> ch, const QString& deviceId);
    void    dropClient(const QString& clientId);

    Auth&                              auth_;
    QHash<QString, Client>             clients_;   // clientId -> state
};

} // namespace polymath
