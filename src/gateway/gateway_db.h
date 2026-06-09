#pragma once
//
// gateway_db — schema + settings bootstrap that the gateway owns at runtime.
//
// The core schema (src/core/schema.h) intentionally does NOT know about mobile
// access, so this module brings its own `devices` table and its own settings
// keys.  ensureSchema() is idempotent (CREATE TABLE IF NOT EXISTS) and is called
// from GatewayService::start().
//
//   DEFERRED WIRING: fold the `devices` CREATE TABLE into core/schema.h (and bump
//   kSchemaVersion) once the module is wired in, so it participates in the normal
//   migration path.  Until then we create it lazily here.
//
// Settings live in the EXISTING `settings` key-value table under the gateway.*
// namespace, so the Privacy/Settings UI and Config facade can read them too:
//
//   gateway.home_id         opaque routing id for this home (UUID, generated once)
//   gateway.secret          shared secret authenticating the home to the relay
//   gateway.relay_url       wss base url of the relay (e.g. "wss://relay.example")
//   gateway.remote_enabled  "1"/"0" — dial the relay? (OFF by default, §4 privacy)
//   gateway.port            local HTTP/WS listen port (default 8765)
//
#include <string>

namespace polymath {

class Database;

namespace gwkeys {
    inline constexpr const char* HomeId        = "gateway.home_id";
    inline constexpr const char* Secret        = "gateway.secret";
    inline constexpr const char* RelayUrl      = "gateway.relay_url";
    inline constexpr const char* RemoteEnabled = "gateway.remote_enabled";
    inline constexpr const char* Port          = "gateway.port";
}

// Default local listen port (matches LAN_PORT in app/src/api/contract.ts).
inline constexpr int kDefaultPort = 8765;

class GatewayDb {
public:
    // Creates the `devices` table if absent and seeds gateway.* settings on
    // first run (generating home_id + secret).  Safe to call repeatedly.
    static void ensureSchema(Database& db);

    // --- typed accessors over the settings table -------------------------
    static std::string homeId(Database& db);
    static std::string secret(Database& db);
    static std::string relayUrl(Database& db);
    static bool        remoteEnabled(Database& db);
    static int         port(Database& db);

    static void setRelayUrl(Database& db, const std::string& url);
    static void setRemoteEnabled(Database& db, bool on);
    static void setPort(Database& db, int port);

private:
    // Generates a fresh value if the key is missing/empty; returns the value.
    static std::string ensureSecretKey(Database& db, const char* key);
};

} // namespace polymath
