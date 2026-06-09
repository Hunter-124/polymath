#include "gateway_service.h"

#include "auth.h"
#include "bridge.h"
#include "config.h"
#include "database.h"
#include "gateway_db.h"
#include "http_router.h"
#include "http_server.h"
#include "json_map.h"
#include "logging.h"
#include "relay_client.h"
#include "ws_hub.h"

#include <nlohmann/json.hpp>

#include <QHostInfo>
#include <QUrl>
#include <QUrlQuery>

namespace polymath {

using nlohmann::json;
namespace jm = json_map;

GatewayService::GatewayService(IAssistantBridge& bridge, Database& db, Config& cfg, QObject* parent)
    : QObject(parent), bridge_(bridge), db_(db), cfg_(cfg) {}

GatewayService::~GatewayService() { stop(); }

// ─── lifecycle ───────────────────────────────────────────────────────────────

void GatewayService::start() {
    if (started_) return;

    // 1. schema + settings (devices table, gateway.* keys, home_id/secret).
    GatewayDb::ensureSchema(db_);

    // 2. components.  Order: auth -> router -> hub -> server/relay.
    auth_   = std::make_unique<Auth>(db_, GatewayDb::secret(db_));
    router_ = std::make_unique<HttpRouter>(bridge_, db_, cfg_, *auth_);
    router_->setStartTime(jm::nowUnix());

    hub_    = std::make_unique<WsHub>(*auth_, this);
    connect(hub_.get(), &WsHub::clientCountChanged, this,
            &GatewayService::connectedClientsChanged);

    server_ = std::make_unique<HttpServer>(*router_, *hub_, this);
    relay_  = std::make_unique<RelayClient>(*router_, *hub_, db_, this);
    connect(relay_.get(), &RelayClient::connectedChanged, this, [](bool up) {
        PM_INFO("gateway: relay tunnel {}", up ? "up" : "down");
    });

    // 3. local listener.
    const quint16 port = static_cast<quint16>(GatewayDb::port(db_));
    if (!server_->listen(port)) {
        PM_ERROR("gateway: failed to start HTTP server on port {}", port);
        // Keep the service "started" so the relay can still run; the desktop UI
        // surfaces the bind failure separately.
    }

    // 4. relay (dials out only if remote access is enabled).
    relay_->start();

    started_ = true;
    PM_INFO("gateway: started (port={}, remote_enabled={})",
            port, GatewayDb::remoteEnabled(db_));
}

void GatewayService::stop() {
    if (!started_) return;
    if (relay_)  relay_->stop();
    if (server_) server_->stop();
    relay_.reset();
    server_.reset();
    hub_.reset();
    router_.reset();
    auth_.reset();
    started_ = false;
    PM_INFO("gateway: stopped");
}

// ─── pairing helpers ─────────────────────────────────────────────────────────

std::string GatewayService::buildPairingPayload() {
    // A fresh single-use code per QR render.
    const QString code = auth_ ? auth_->newPairCode() : QString();

    // Best-effort LAN host for the fast path; falls back to mDNS name.  The app's
    // transport tries LAN first, then relay (transport.ts).
    QString lanHost = QHostInfo::localHostName();
    if (!lanHost.endsWith(QLatin1String(".local")))
        lanHost = QStringLiteral("polymath.local");

    json payload{
        {"relay_url", GatewayDb::relayUrl(db_)},     // "" => LAN-only pairing
        {"home_id",   GatewayDb::homeId(db_)},
        {"pair_code", code.toStdString()},
        {"lan_host",  lanHost.toStdString()},
        {"lan_port",  GatewayDb::port(db_)},
    };
    return payload.dump();
}

QString GatewayService::pairingPayloadJson() {
    return QString::fromStdString(buildPairingPayload());
}

QString GatewayService::pairingDeepLink() {
    const json p = json::parse(buildPairingPayload());

    QUrl url;
    url.setScheme(QStringLiteral("polymath"));
    url.setHost(QStringLiteral("pair"));

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("home_id"),
                   QString::fromStdString(p.value("home_id", std::string())));
    q.addQueryItem(QStringLiteral("code"),
                   QString::fromStdString(p.value("pair_code", std::string())));
    if (!p.value("relay_url", std::string()).empty())
        q.addQueryItem(QStringLiteral("relay"),
                       QString::fromStdString(p.value("relay_url", std::string())));
    q.addQueryItem(QStringLiteral("host"),
                   QString::fromStdString(p.value("lan_host", std::string())));
    q.addQueryItem(QStringLiteral("port"),
                   QString::number(p.value("lan_port", int(kDefaultPort))));
    url.setQuery(q);
    return url.toString(QUrl::FullyEncoded);
}

// ─── remote toggle + status ──────────────────────────────────────────────────

void GatewayService::setRemoteEnabled(bool enabled) {
    // Persist immediately (the settings table is thread-safe). This invokable is
    // called from the desktop UI thread, but relay_ owns a QWebSocket + QTimers
    // that must only be driven on the gateway thread — so marshal the actual
    // open/close onto this object's thread (AutoConnection: direct on the gateway
    // thread, queued from the UI thread).
    GatewayDb::setRemoteEnabled(db_, enabled);
    QMetaObject::invokeMethod(this, [this, enabled] {
        if (relay_) relay_->setEnabled(enabled);
    });
    emit remoteEnabledChanged(enabled);
    PM_INFO("gateway: remote access {}", enabled ? "enabled" : "disabled");
}

bool GatewayService::remoteEnabled() const { return GatewayDb::remoteEnabled(db_); }

int GatewayService::connectedClients() const { return hub_ ? hub_->clientCount() : 0; }

} // namespace polymath
