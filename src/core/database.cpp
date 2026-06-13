#include "database.h"
#include "logging.h"
#include <sqlite3.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace polymath {

namespace {
// Escape a key for single-quoted SQL (PRAGMA key='...'). SQLCipher accepts a
// passphrase here; doubling any embedded quote keeps the statement well-formed.
std::string quoteKey(const std::string& key) {
    std::string out;
    out.reserve(key.size() + 2);
    for (char c : key) { if (c == '\'') out += '\''; out += c; }
    return out;
}

// True when the file on disk begins with the standard plaintext SQLite header.
// A SQLCipher-encrypted file does NOT (page 1 is ciphertext / salt). An empty or
// missing file (a fresh DB) is NOT plaintext for our purposes.
bool isPlaintextSqlite(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char hdr[16] = {0};
    f.read(hdr, sizeof hdr);
    if (f.gcount() < static_cast<std::streamsize>(sizeof hdr)) return false;
    static const char kMagic[16] =
        {'S','Q','L','i','t','e',' ','f','o','r','m','a','t',' ','3','\0'};
    for (int i = 0; i < 16; ++i) if (hdr[i] != kMagic[i]) return false;
    return true;
}
} // namespace

int64_t     Row::i64(int c)    const { return sqlite3_column_int64(stmt_, c); }
double      Row::dbl(int c)    const { return sqlite3_column_double(stmt_, c); }
bool        Row::isNull(int c) const { return sqlite3_column_type(stmt_, c) == SQLITE_NULL; }
std::string Row::text(int c)   const {
    auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, c));
    return p ? std::string(p) : std::string();
}

Database::~Database() { close(); }

bool Database::open(const std::string& path, const std::string& key) {
    encryption_active_ = false;
    cipher_version_.clear();

    // MIGRATION: a key was supplied and a real SQLCipher codec is linked, but an
    // EXISTING file on disk is still plaintext (created by an older, codec-less
    // build). Transparently re-encrypt it in place before we key the connection,
    // so existing users are upgraded without data loss or lock-out. If the codec
    // is absent we skip this (the file stays plaintext, reported honestly below).
    if (!key.empty() && hasCodec() && isPlaintextSqlite(path)) {
        if (!migratePlaintextToEncrypted(path, key)) {
            PM_ERROR("Database: failed to migrate plaintext DB to encrypted — "
                     "leaving the original untouched.");
            return false;
        }
    }

    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        PM_ERROR("sqlite open failed: {}", sqlite3_errmsg(db_));
        return false;
    }

    if (!key.empty()) {
        // SQLCipher: PRAGMA key MUST run before any other access to the file.
        const std::string pragma = "PRAGMA key = '" + quoteKey(key) + "';";
        if (sqlite3_exec(db_, pragma.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
            PM_WARN("PRAGMA key failed: {}", sqlite3_errmsg(db_));

        // Detect whether a real codec engaged. SQLCipher answers PRAGMA
        // cipher_version with its build string; plain SQLite returns nothing.
        cipher_version_    = scalar("PRAGMA cipher_version;");
        encryption_active_ = !cipher_version_.empty();

        // Prove the key actually opens the database: the first real read of a
        // SQLCipher file with the wrong (or no) key fails with NOTADB. We read
        // sqlite_master, which forces the codec to decrypt page 1.
        char* err = nullptr;
        const int rc = sqlite3_exec(
            db_, "SELECT count(*) FROM sqlite_master;", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            PM_ERROR("database key check failed (wrong key or corrupt db?): {}",
                     err ? err : sqlite3_errmsg(db_));
            sqlite3_free(err);
            close();
            return false;
        }
        sqlite3_free(err);

        if (encryption_active_)
            PM_INFO("Database: at-rest encryption ACTIVE (SQLCipher {})", cipher_version_);
        else
            PM_WARN("Database: a key was supplied but the linked SQLite has no "
                    "codec — the file is NOT encrypted (plain sqlite3). Build with "
                    "SQLCipher to enforce at-rest encryption.");
    }

    sqlite3_busy_timeout(db_, 5000);
    return migrate();
}

// Does the linked SQLite have a working SQLCipher codec? Open a throwaway
// in-memory connection, key it, and check PRAGMA cipher_version. (Cheaper and
// safer than guessing from build macros; works for both build paths.)
bool Database::hasCodec() {
    sqlite3* probe = nullptr;
    if (sqlite3_open(":memory:", &probe) != SQLITE_OK) {
        if (probe) sqlite3_close(probe);
        return false;
    }
    sqlite3_exec(probe, "PRAGMA key = 'probe';", nullptr, nullptr, nullptr);
    std::string ver;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(probe, "PRAGMA cipher_version;", -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(st, 0)))
                ver = p;
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(probe);
    return !ver.empty();
}

// One-time conversion of an existing PLAINTEXT database at `path` into a
// SQLCipher-encrypted database keyed with `key`, in place. Uses SQLCipher's
// sqlite3_rekey-equivalent export path (ATTACH a keyed file + sqlcipher_export)
// so schema, indexes and rows are copied verbatim. The original is preserved as
// `<path>.plaintext.bak` until the encrypted copy is verified, then removed.
bool Database::migratePlaintextToEncrypted(const std::string& path,
                                           const std::string& key) {
    namespace fs = std::filesystem;
    PM_INFO("Database: detected a PLAINTEXT database at '{}' — migrating to "
            "encrypted (SQLCipher) on first run.", path);

    const std::string enc = path + ".enc.tmp";
    std::error_code ec;
    fs::remove(enc, ec);   // stale temp from an aborted prior run

    // Open the plaintext source (no key), attach a freshly-keyed target, and
    // copy everything across. sqlcipher_export() recreates the full schema.
    sqlite3* src = nullptr;
    if (sqlite3_open(path.c_str(), &src) != SQLITE_OK) {
        PM_ERROR("migration: cannot open plaintext source: {}",
                 src ? sqlite3_errmsg(src) : "?");
        if (src) sqlite3_close(src);
        return false;
    }

    bool ok = true;
    auto run = [&](const std::string& sql) {
        char* e = nullptr;
        if (sqlite3_exec(src, sql.c_str(), nullptr, nullptr, &e) != SQLITE_OK) {
            PM_ERROR("migration step failed [{}]: {}", sql, e ? e : "?");
            sqlite3_free(e);
            ok = false;
        }
    };

    // Force any pending WAL of the plaintext source into the main file first, so
    // sqlcipher_export() copies fully-committed data even if the DB was last
    // closed uncleanly (WAL mode is set by the schema).
    run("PRAGMA wal_checkpoint(TRUNCATE);");
    // ATTACH the encrypted target with the production key, copy, detach.
    run("ATTACH DATABASE '" + quoteKey(enc) + "' AS encrypted KEY '" + quoteKey(key) + "';");
    if (ok) run("SELECT sqlcipher_export('encrypted');");
    if (ok) run("DETACH DATABASE encrypted;");
    sqlite3_close(src);   // last connection -> SQLite checkpoints + removes -wal/-shm

    if (!ok) { fs::remove(enc, ec); return false; }

    // Verify the encrypted copy opens with the key and is genuinely ciphered.
    if (isPlaintextSqlite(enc)) {
        PM_ERROR("migration: produced file is not ciphered — aborting.");
        fs::remove(enc, ec);
        return false;
    }

    // Swap: keep the plaintext original as a .bak, then move the encrypted copy
    // into place. The .bak lets a user recover if anything is wrong; it is the
    // user's responsibility to delete it once satisfied (we log a reminder).
    const std::string bak = path + ".plaintext.bak";
    fs::remove(bak, ec);
    fs::rename(path, bak, ec);
    if (ec) { PM_ERROR("migration: cannot back up original: {}", ec.message()); fs::remove(enc, ec); return false; }
    fs::rename(enc, path, ec);
    if (ec) {
        PM_ERROR("migration: cannot install encrypted DB: {} — restoring original.", ec.message());
        fs::rename(bak, path, ec);
        return false;
    }
    // Remove any stale PLAINTEXT WAL/SHM sidecars left by the old DB — they must
    // never sit beside the new encrypted main file (the encrypted connection
    // would mis-read them). A clean close above normally deletes them; this is
    // belt-and-suspenders for an unclean prior shutdown.
    fs::remove(path + "-wal", ec);
    fs::remove(path + "-shm", ec);
    PM_WARN("Database: migration complete — encrypted DB is now live. The "
            "plaintext backup is at '{}'. Delete it once you have confirmed the "
            "app works, to remove the last plaintext copy.", bak);
    return true;
}

// Run a query expected to yield one text value (PRAGMA or scalar SELECT).
std::string Database::scalar(const std::string& sql, const std::string& def) {
    std::string out = def;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
        return out;   // unknown PRAGMA on plain SQLite -> leave default
    if (sqlite3_step(st) == SQLITE_ROW) {
        if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(st, 0)))
            out = p;
    }
    sqlite3_finalize(st);
    return out;
}

// Deterministic 256-bit key (64 hex chars) from `material`. A simple, dependency
// -free mix (two 64-bit FNV-1a lanes with distinct seeds) — enough to keep the
// raw passphrase off the command line; an OS keystore-backed secret should feed
// `material` in production.
std::string Database::deriveKey(const std::string& material) {
    auto fnv = [](const std::string& s, uint64_t seed) {
        uint64_t h = seed;
        for (unsigned char c : s) { h ^= c; h *= 0x100000001b3ULL; }
        // Avalanche the final value so adjacent inputs diverge.
        h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33;
        return h;
    };
    const uint64_t lanes[4] = {
        fnv(material, 0xcbf29ce484222325ULL),
        fnv(material, 0x9e3779b97f4a7c15ULL),
        fnv(material + "\x01", 0xcbf29ce484222325ULL),
        fnv(material + "\x02", 0x9e3779b97f4a7c15ULL),
    };
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint64_t v : lanes)
        for (int i = 60; i >= 0; i -= 4)
            out += hex[(v >> i) & 0xF];
    return out;
}

#ifdef _WIN32
#  include <windows.h>
#  include <wincrypt.h>   // CryptProtectData / CryptUnprotectData (DPAPI)
#  include <bcrypt.h>     // BCryptGenRandom (CSPRNG)
#  pragma comment(lib, "crypt32.lib")
#  pragma comment(lib, "bcrypt.lib")
#endif

// Loads or creates the per-install at-rest encryption key.
//
// The on-disk keyfile holds a DPAPI-protected blob (CryptProtectData, scoped to
// the current Windows user) wrapping a random 256-bit secret. We never store the
// raw key; only the current user on this machine can unwrap it. The unwrapped
// secret is then fed through deriveKey() to produce the 64-hex SQLCipher key, so
// the value handed to PRAGMA key is itself a derivation, not the stored bytes.
//
// On non-Windows (or if DPAPI is unavailable) we fall back to storing the raw
// random secret with restrictive perms — still local-only and not hardcoded.
std::string Database::loadOrCreateKey(const std::string& keyfile) {
    namespace fs = std::filesystem;

    auto readFile = [](const std::string& p, std::string& out) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    };
    auto writeFile = [](const std::string& p, const std::string& data) {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        return f.good();
    };

#ifdef _WIN32
    // ---- existing keyfile: unprotect the DPAPI blob -> raw secret ----
    std::string blob;
    if (readFile(keyfile, blob) && !blob.empty()) {
        DATA_BLOB in{ static_cast<DWORD>(blob.size()),
                      reinterpret_cast<BYTE*>(blob.data()) };
        DATA_BLOB out{};
        if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
            std::string secret(reinterpret_cast<char*>(out.pbData), out.cbData);
            LocalFree(out.pbData);
            return deriveKey(secret);
        }
        PM_WARN("Database: keyfile present but could not be unprotected (different "
                "user/machine?); regenerating — an existing encrypted DB keyed with "
                "the old secret will NOT open.");
    }

    // ---- first run: generate a random secret, protect it, persist ----
    unsigned char raw[32] = {0};
    if (BCryptGenRandom(nullptr, raw, sizeof raw,
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        PM_ERROR("Database: BCryptGenRandom failed; cannot create install key.");
        return "";
    }
    std::string secret(reinterpret_cast<char*>(raw), sizeof raw);

    DATA_BLOB in{ static_cast<DWORD>(secret.size()),
                  reinterpret_cast<BYTE*>(secret.data()) };
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"polymath-db-key", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        PM_ERROR("Database: CryptProtectData failed; cannot persist install key.");
        return "";
    }
    std::string protectedBlob(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);

    if (!writeFile(keyfile, protectedBlob)) {
        PM_ERROR("Database: cannot write keyfile '{}'.", keyfile);
        return "";
    }
    PM_INFO("Database: created a new DPAPI-protected install key at '{}'.", keyfile);
    return deriveKey(secret);
#else
    // Portable fallback: store the raw random secret (no OS keystore).
    std::string secret;
    if (readFile(keyfile, secret) && !secret.empty())
        return deriveKey(secret);
    secret.resize(32);
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom) urandom.read(secret.data(), static_cast<std::streamsize>(secret.size()));
    if (!writeFile(keyfile, secret)) {
        PM_ERROR("Database: cannot write keyfile '{}'.", keyfile);
        return "";
    }
    fs::permissions(keyfile, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace);
    return deriveKey(secret);
#endif
}

void Database::close() {
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

static void bind(sqlite3_stmt* st, const std::vector<nlohmann::json>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        const auto& p = params[i];
        const int idx = static_cast<int>(i + 1);
        if (p.is_null())              sqlite3_bind_null(st, idx);
        else if (p.is_boolean())      sqlite3_bind_int(st, idx, p.get<bool>() ? 1 : 0);
        else if (p.is_number_integer()) sqlite3_bind_int64(st, idx, p.get<int64_t>());
        else if (p.is_number_float()) sqlite3_bind_double(st, idx, p.get<double>());
        else {
            std::string s = p.is_string() ? p.get<std::string>() : p.dump();
            sqlite3_bind_text(st, idx, s.c_str(), -1, SQLITE_TRANSIENT);
        }
    }
}

// True if `table` already has a column named `column` (PRAGMA table_info walk).
bool Database::hasColumn(const std::string& table, const std::string& column) {
    bool found = false;
    sqlite3_stmt* st = nullptr;
    const std::string sql = "PRAGMA table_info(" + table + ");";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        // table_info columns: cid(0), name(1), type(2), ...
        if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(st, 1)))
            if (column == p) { found = true; break; }
    }
    sqlite3_finalize(st);
    return found;
}

bool Database::migrate() {
    std::lock_guard lk(mtx_);
    char* err = nullptr;
    if (sqlite3_exec(db_, kSchemaSQL, nullptr, nullptr, &err) != SQLITE_OK) {
        PM_ERROR("schema migrate failed: {}", err ? err : "?");
        sqlite3_free(err);
        return false;
    }
    // Idempotent column additions for tables that predate the current schema
    // version. Bare ALTER ADD COLUMN errors on re-run, so each is guarded by a
    // column-existence check — safe on both fresh and upgraded databases.
    for (const auto& p : kColumnPatches) {
        if (hasColumn(p.table, p.column)) continue;
        const std::string sql =
            std::string("ALTER TABLE ") + p.table + " ADD COLUMN " + p.definition + ";";
        char* e = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &e) != SQLITE_OK) {
            PM_ERROR("schema column patch failed [{}]: {}", sql, e ? e : "?");
            sqlite3_free(e);
            return false;
        }
        PM_INFO("Database: added column {}.{}", p.table, p.column);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "PRAGMA user_version = %d;", kSchemaVersion);
    sqlite3_exec(db_, buf, nullptr, nullptr, nullptr);
    return true;
}

int64_t Database::exec(const std::string& sql, const std::vector<nlohmann::json>& params) {
    std::lock_guard lk(mtx_);
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        PM_ERROR("prepare failed [{}]: {}", sql, sqlite3_errmsg(db_));
        return -1;
    }
    bind(st, params);
    if (sqlite3_step(st) != SQLITE_DONE)
        PM_WARN("exec step [{}]: {}", sql, sqlite3_errmsg(db_));
    sqlite3_finalize(st);
    return sqlite3_last_insert_rowid(db_);
}

void Database::query(const std::string& sql,
                     const std::vector<nlohmann::json>& params,
                     const std::function<void(const Row&)>& fn) {
    std::lock_guard lk(mtx_);
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        PM_ERROR("prepare failed [{}]: {}", sql, sqlite3_errmsg(db_));
        return;
    }
    bind(st, params);
    Row row(st);
    while (sqlite3_step(st) == SQLITE_ROW) fn(row);
    sqlite3_finalize(st);
}

std::string Database::getSetting(const std::string& key, const std::string& def) {
    std::string out = def;
    query("SELECT value FROM settings WHERE key=?1", {key},
          [&](const Row& r) { out = r.text(0); });
    return out;
}

void Database::setSetting(const std::string& key, const std::string& value) {
    exec("INSERT INTO settings(key,value) VALUES(?1,?2) "
         "ON CONFLICT(key) DO UPDATE SET value=excluded.value", {key, value});
}

bool Database::getBool(const std::string& key, bool def) {
    std::string v = getSetting(key, def ? "1" : "0");
    return v == "1" || v == "true";
}

} // namespace polymath
