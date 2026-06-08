#pragma once
//
// Auth — device tokens + QR pairing, all on top of QtCore (no new dependency).
//
// TOKEN FORMAT (a compact, JWT-ish HMAC token):
//
//     base64url(payload_json) "." base64url(HMAC-SHA256(payload_b64, secret))
//
//   payload_json = { "device_id": "<uuid>", "role": "owner|guest", "iat": <unix> }
//
// We sign with the per-home gateway secret (gateway.secret in settings).  The
// token is stateless to verify (HMAC) BUT revocable: verifyToken() also checks
// that the device_id still has a row in the `devices` table, so revoking a
// device (deleting its row) rejects its token immediately — exactly the
// behaviour promised in REMOTE_ACCESS.md §3.
//
// PAIRING:
//   * The desktop shows a QR carrying { relay_url, home_id, pair_code }.
//   * pair_code is a short, single-use code with a 5-minute TTL, kept in memory
//     only (a relay/app restart simply invalidates outstanding codes).
//   * The phone POSTs { code, device_name, pubkey } to /pair; verifyPairCode()
//     burns the code, createDevice() inserts a row, issueToken() returns a token.
//
#include <QByteArray>
#include <QHash>
#include <QString>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace polymath {

class Database;

// Decoded, verified token claims.
struct TokenClaims {
    QString device_id;
    QString role;        // "owner" | "guest"
    int64_t iat = 0;     // issued-at, unix seconds
};

// A row from the devices table, as the desktop device manager sees it.
struct DeviceRow {
    QString device_id;
    QString name;
    QString role;
    QString platform;
    int64_t created_at = 0;
    int64_t last_seen  = 0;
};

class Auth {
public:
    // `db` supplies the devices table; `secret` signs/verifies tokens (the
    // gateway.secret value).  Auth keeps a reference to the DB but no ownership.
    Auth(Database& db, std::string secret);

    // --- tokens ----------------------------------------------------------

    // Mint a token for an existing device row.
    QString issueToken(const QString& deviceId, const QString& role);

    // Verify a token (HMAC + device-still-exists).  Accepts either a raw token
    // or an "Authorization: Bearer <token>" value (the leading "Bearer " is
    // stripped).  Returns the claims on success.  Side effect: bumps last_seen.
    std::optional<TokenClaims> verifyToken(const QString& bearerOrToken);

    // --- pairing ---------------------------------------------------------

    // Create a single-use pairing code (TTL 5 min) and return it.  Called when
    // the desktop opens Settings ▸ Mobile Access / renders the QR.
    QString newPairCode();

    // Validate (and consume) a pairing code.  Returns true exactly once per
    // valid, unexpired code.
    bool verifyPairCode(const QString& code);

    // Insert a devices row and return its generated device_id.  `role` defaults
    // to owner; pass "guest" for limited-scope devices.
    QString createDevice(const QString& name,
                         const QString& platform,
                         const QString& pubkey,
                         const QString& role = QStringLiteral("owner"));

    // --- device CRUD (desktop manager + /devices routes) -----------------

    std::vector<DeviceRow> listDevices();

    // Delete a device row (revokes its token instantly).  Returns true if a row
    // was removed.
    bool revokeDevice(const QString& deviceId);

    // True if a device row exists (used by verifyToken and tests).
    bool deviceExists(const QString& deviceId);

    // TTL for pairing codes.
    static constexpr int kPairTtlSeconds = 5 * 60;

private:
    static QByteArray b64urlEncode(const QByteArray& in);
    static QByteArray b64urlDecode(const QByteArray& in);
    QByteArray sign(const QByteArray& payloadB64) const;
    void touchLastSeen(const QString& deviceId);
    void sweepExpiredCodes();   // drop timed-out codes (called opportunistically)

    Database&   db_;
    QByteArray  secret_;

    struct PendingCode { std::chrono::steady_clock::time_point expires; };
    std::mutex                    codes_mtx_;
    QHash<QString, PendingCode>   codes_;   // code -> expiry (in-memory, single-use)
};

} // namespace polymath
