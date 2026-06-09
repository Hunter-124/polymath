#include "auth.h"

#include "database.h"
#include "logging.h"
#include "types.h"   // to_unix / Clock

#include <nlohmann/json.hpp>

#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QUuid>

namespace polymath {

using nlohmann::json;

Auth::Auth(Database& db, std::string secret)
    : db_(db), secret_(QByteArray::fromStdString(secret)) {}

// ─── base64url (no padding) ─────────────────────────────────────────────────

QByteArray Auth::b64urlEncode(const QByteArray& in) {
    return in.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray Auth::b64urlDecode(const QByteArray& in) {
    return QByteArray::fromBase64(in, QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

// ─── token signing / verification ───────────────────────────────────────────

QByteArray Auth::sign(const QByteArray& payloadB64) const {
    return QMessageAuthenticationCode::hash(payloadB64, secret_, QCryptographicHash::Sha256);
}

QString Auth::issueToken(const QString& deviceId, const QString& role) {
    json payload{
        {"device_id", deviceId.toStdString()},
        {"role",      role.toStdString()},
        {"iat",       to_unix(Clock::now())},
    };
    const QByteArray payloadB64 =
        b64urlEncode(QByteArray::fromStdString(payload.dump()));
    const QByteArray sigB64 = b64urlEncode(sign(payloadB64));
    return QString::fromLatin1(payloadB64 + "." + sigB64);
}

std::optional<TokenClaims> Auth::verifyToken(const QString& bearerOrToken) {
    QString tok = bearerOrToken.trimmed();
    if (tok.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
        tok = tok.mid(7).trimmed();
    if (tok.isEmpty()) return std::nullopt;

    const int dot = tok.indexOf('.');
    if (dot <= 0 || dot == tok.size() - 1) return std::nullopt;

    const QByteArray payloadB64 = tok.left(dot).toLatin1();
    const QByteArray sigB64     = tok.mid(dot + 1).toLatin1();

    // Constant-time HMAC comparison (QByteArray::operator== is length-then-memcmp;
    // QMessageAuthenticationCode has no timing-safe compare, but both operands are
    // fixed-length 32-byte digests here, so the length branch never varies).
    const QByteArray expected = b64urlEncode(sign(payloadB64));
    if (sigB64.size() != expected.size()) return std::nullopt;
    {
        // XOR-accumulate to avoid an early-out byte compare.
        quint8 diff = 0;
        for (int i = 0; i < expected.size(); ++i)
            diff |= static_cast<quint8>(sigB64[i] ^ expected[i]);
        if (diff != 0) return std::nullopt;
    }

    // Signature is valid — decode the claims.
    TokenClaims claims;
    try {
        const json p = json::parse(b64urlDecode(payloadB64).toStdString());
        claims.device_id = QString::fromStdString(p.value("device_id", std::string()));
        claims.role      = QString::fromStdString(p.value("role", std::string("owner")));
        claims.iat       = p.value("iat", int64_t(0));
    } catch (...) {
        return std::nullopt;
    }
    if (claims.device_id.isEmpty()) return std::nullopt;

    // Revocation check: the device must still exist.
    if (!deviceExists(claims.device_id)) return std::nullopt;

    touchLastSeen(claims.device_id);
    return claims;
}

// ─── pairing codes (in-memory, single-use, TTL) ─────────────────────────────

void Auth::sweepExpiredCodes() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = codes_.begin(); it != codes_.end();) {
        if (it.value().expires <= now) it = codes_.erase(it);
        else ++it;
    }
}

QString Auth::newPairCode() {
    // 8 uppercase base32-ish chars: short enough to render large in a QR yet
    // ample entropy for a 5-minute single-use window.
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no I,O,0,1
    QString code;
    code.reserve(8);
    auto* rng = QRandomGenerator::system();
    for (int i = 0; i < 8; ++i)
        code.append(QLatin1Char(alphabet[rng->bounded(int(sizeof(alphabet) - 1))]));

    std::lock_guard lk(codes_mtx_);
    sweepExpiredCodes();
    codes_.insert(code, PendingCode{
        std::chrono::steady_clock::now() + std::chrono::seconds(kPairTtlSeconds)});
    return code;
}

bool Auth::verifyPairCode(const QString& code) {
    std::lock_guard lk(codes_mtx_);
    sweepExpiredCodes();
    auto it = codes_.find(code.trimmed());
    if (it == codes_.end()) return false;
    const bool live = it.value().expires > std::chrono::steady_clock::now();
    codes_.erase(it);    // single-use: burn it regardless
    return live;
}

// ─── device CRUD ────────────────────────────────────────────────────────────

QString Auth::createDevice(const QString& name,
                           const QString& platform,
                           const QString& pubkey,
                           const QString& role) {
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const int64_t now = to_unix(Clock::now());
    db_.exec(
        "INSERT INTO devices(id,name,role,platform,pubkey,created_at,last_seen) "
        "VALUES(?1,?2,?3,?4,?5,?6,?6)",
        {id.toStdString(),
         name.toStdString(),
         role.toStdString(),
         platform.toStdString(),
         pubkey.toStdString(),
         now});
    PM_INFO("gateway: paired device {} ({}, {})",
            id.toStdString(), name.toStdString(), role.toStdString());
    return id;
}

std::vector<DeviceRow> Auth::listDevices() {
    std::vector<DeviceRow> out;
    db_.query(
        "SELECT id,name,role,platform,created_at,last_seen FROM devices "
        "ORDER BY created_at DESC",
        {},
        [&](const Row& r) {
            DeviceRow d;
            d.device_id  = QString::fromStdString(r.text(0));
            d.name       = QString::fromStdString(r.text(1));
            d.role       = QString::fromStdString(r.text(2));
            d.platform   = QString::fromStdString(r.text(3));
            d.created_at = r.i64(4);
            d.last_seen  = r.i64(5);
            out.push_back(std::move(d));
        });
    return out;
}

bool Auth::revokeDevice(const QString& deviceId) {
    if (!deviceExists(deviceId)) return false;
    db_.exec("DELETE FROM devices WHERE id=?1", {deviceId.toStdString()});
    PM_INFO("gateway: revoked device {}", deviceId.toStdString());
    return true;
}

bool Auth::deviceExists(const QString& deviceId) {
    bool exists = false;
    db_.query("SELECT 1 FROM devices WHERE id=?1 LIMIT 1",
              {deviceId.toStdString()},
              [&](const Row&) { exists = true; });
    return exists;
}

void Auth::touchLastSeen(const QString& deviceId) {
    db_.exec("UPDATE devices SET last_seen=?1 WHERE id=?2",
             {to_unix(Clock::now()), deviceId.toStdString()});
}

} // namespace polymath
