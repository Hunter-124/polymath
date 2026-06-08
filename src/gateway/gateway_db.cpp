#include "gateway_db.h"
#include "database.h"
#include "logging.h"

#include <QRandomGenerator>
#include <QUuid>

namespace polymath {

// The devices table: one row per paired phone/web client.  `pubkey` holds the
// optional base64 X25519 key for the future E2E layer (§4 REMOTE_ACCESS).
//   DEFERRED: move this into core/schema.h and bump kSchemaVersion at wiring.
static constexpr const char* kDevicesSQL = R"SQL(
CREATE TABLE IF NOT EXISTS devices (
    id          TEXT PRIMARY KEY,            -- device_id (UUID), embedded in the token
    name        TEXT NOT NULL,               -- human label ("George's iPhone")
    role        TEXT NOT NULL DEFAULT 'owner', -- owner|guest (token scope)
    platform    TEXT DEFAULT '',             -- ios|android|web
    pubkey      TEXT DEFAULT '',             -- base64 X25519 (optional E2E)
    created_at  INTEGER NOT NULL,
    last_seen   INTEGER NOT NULL DEFAULT 0
);
)SQL";

void GatewayDb::ensureSchema(Database& db) {
    db.exec(kDevicesSQL);

    // Generate the home identity + relay secret exactly once.  Both are opaque
    // and never leave the house except: home_id travels in pairing payloads and
    // relay hellos; secret travels ONLY in the relay hello (authenticating the
    // home to the relay).  See registry.ts for the relay-side check.
    ensureSecretKey(db, gwkeys::HomeId);
    ensureSecretKey(db, gwkeys::Secret);

    // Remote access is OFF by default (privacy posture): the gateway will not
    // dial the relay until the user flips this in Settings ▸ Mobile Access.
    if (db.getSetting(gwkeys::RemoteEnabled, "").empty())
        db.setSetting(gwkeys::RemoteEnabled, "0");

    // Relay URL is empty by default ⇒ LAN-only pairing until the user provides
    // one (or the hosted default is filled in during wiring).
    if (db.getSetting(gwkeys::RelayUrl, "\0__none__").empty())
        db.setSetting(gwkeys::RelayUrl, "");

    if (db.getSetting(gwkeys::Port, "").empty())
        db.setSetting(gwkeys::Port, std::to_string(kDefaultPort));

    PM_INFO("gateway: schema ready (home_id={})", homeId(db));
}

std::string GatewayDb::ensureSecretKey(Database& db, const char* key) {
    std::string cur = db.getSetting(key, "");
    if (!cur.empty()) return cur;

    std::string val;
    if (std::string(key) == gwkeys::HomeId) {
        // A compact, URL-safe routing id.
        val = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    } else {
        // 256 bits of CSPRNG, hex-encoded, for the relay shared secret.
        quint64 a = QRandomGenerator::system()->generate64();
        quint64 b = QRandomGenerator::system()->generate64();
        val = QStringLiteral("%1%2")
                  .arg(a, 16, 16, QLatin1Char('0'))
                  .arg(b, 16, 16, QLatin1Char('0'))
                  .toStdString();
    }
    db.setSetting(key, val);
    return val;
}

std::string GatewayDb::homeId(Database& db)   { return db.getSetting(gwkeys::HomeId, ""); }
std::string GatewayDb::secret(Database& db)   { return db.getSetting(gwkeys::Secret, ""); }
std::string GatewayDb::relayUrl(Database& db) { return db.getSetting(gwkeys::RelayUrl, ""); }
bool        GatewayDb::remoteEnabled(Database& db) { return db.getBool(gwkeys::RemoteEnabled, false); }

int GatewayDb::port(Database& db) {
    std::string v = db.getSetting(gwkeys::Port, std::to_string(kDefaultPort));
    try { return std::stoi(v); } catch (...) { return kDefaultPort; }
}

void GatewayDb::setRelayUrl(Database& db, const std::string& url) { db.setSetting(gwkeys::RelayUrl, url); }
void GatewayDb::setRemoteEnabled(Database& db, bool on) { db.setSetting(gwkeys::RemoteEnabled, on ? "1" : "0"); }
void GatewayDb::setPort(Database& db, int port) { db.setSetting(gwkeys::Port, std::to_string(port)); }

} // namespace polymath
