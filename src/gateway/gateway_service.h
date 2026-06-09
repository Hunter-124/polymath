#pragma once
//
// GatewayService — the IService that assembles the whole mobile gateway.
//
// Lifecycle (start() runs on the gateway's own QThread, per service.h):
//   1. GatewayDb::ensureSchema  — devices table + gateway.* settings.
//   2. build Auth, HttpRouter, WsHub, HttpServer, RelayClient.
//   3. HttpServer listens on 0.0.0.0:<gateway.port>.
//   4. RelayClient.start() — dials the relay iff gateway.remote_enabled.
// stop() tears all of that down in reverse.
//
// It also exposes pairing helpers the DESKTOP UI (Settings ▸ Mobile Access) uses
// to render a QR:
//   * pairingPayloadJson() — the JSON the QR encodes (PairingPayload).
//   * pairingDeepLink()    — a polymath://pair?... URL (some clients scan this).
// Each call mints a fresh single-use pair code (TTL 5 min).
//
// CONSTRUCTION: takes the bridge + the app's Database + Config by reference (it
// does NOT own them).  AppController will construct one of these on its own
// QThread during wiring (see README "DEFERRED WIRING").
//
#include "service.h"

#include <QObject>
#include <QString>
#include <memory>

namespace polymath {

class IAssistantBridge;
class Database;
class Config;
class Auth;
class HttpRouter;
class HttpServer;
class WsHub;
class RelayClient;

class GatewayService : public QObject, public IService {
    Q_OBJECT
public:
    GatewayService(IAssistantBridge& bridge, Database& db, Config& cfg, QObject* parent = nullptr);
    ~GatewayService() override;

    // IService.
    void start() override;
    void stop() override;
    const char* serviceName() const override { return "gateway"; }

    // --- desktop UI helpers (thread-safe; mint a fresh pair code) ---------

    // The PairingPayload JSON the desktop encodes into a QR:
    //   { relay_url, home_id, pair_code, lan_host, lan_port }
    Q_INVOKABLE QString pairingPayloadJson();

    // A scannable deep link carrying the same fields:
    //   polymath://pair?home_id=..&code=..&relay=..&host=..&port=..
    Q_INVOKABLE QString pairingDeepLink();

    // Toggle remote access at runtime (persists the setting + opens/closes the
    // relay tunnel).  The desktop "Allow remote access" switch calls this.
    Q_INVOKABLE void setRemoteEnabled(bool enabled);
    Q_INVOKABLE bool remoteEnabled() const;

    // Number of currently-connected mobile clients (for the desktop UI).
    Q_INVOKABLE int connectedClients() const;

signals:
    void connectedClientsChanged(int count);
    void remoteEnabledChanged(bool enabled);

private:
    // Build the PairingPayload as a json object (used by both helpers above).
    // Mints a new pair code each call.
    std::string buildPairingPayload();

    IAssistantBridge& bridge_;
    Database&         db_;
    Config&           cfg_;

    std::unique_ptr<Auth>        auth_;
    std::unique_ptr<HttpRouter>  router_;
    std::unique_ptr<WsHub>       hub_;
    std::unique_ptr<HttpServer>  server_;
    std::unique_ptr<RelayClient> relay_;

    bool started_ = false;
};

} // namespace polymath
