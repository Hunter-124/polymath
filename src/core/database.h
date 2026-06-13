#pragma once
//
// Database — thread-safe SQLite wrapper shared by all services.
//
// One process-wide connection in WAL mode, guarded by a mutex.  WAL lets the
// many reader threads proceed concurrently; writes serialize on the mutex.
// Higher-level repositories (memory/store, scheduler/task_queue, ...) build on
// the small exec/query primitives here.  Optionally SQLCipher-encrypted.
//
#include "schema.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace polymath {

// A single result row exposed as a tiny accessor over a prepared statement.
class Row {
public:
    explicit Row(sqlite3_stmt* s) : stmt_(s) {}
    int64_t     i64(int col) const;
    double      dbl(int col) const;
    std::string text(int col) const;
    bool        isNull(int col) const;
private:
    sqlite3_stmt* stmt_;
};

class Database {
public:
    Database() = default;
    ~Database();

    // Opens (creating if needed) and applies the schema. key != "" requests
    // SQLCipher at-rest encryption (PRAGMA key); whether the bytes on disk are
    // actually ciphered depends on the linked SQLite build — see
    // encryptionActive(). Returns false on failure (including a wrong key against
    // an already-encrypted database).
    bool open(const std::string& path, const std::string& key = "");
    void close();

    // True only when a real cipher is engaged on the open connection: a non-empty
    // key was supplied AND the linked library reports a SQLCipher codec version
    // (PRAGMA cipher_version). With plain SQLite this is always false even after a
    // PRAGMA key (the key is silently ignored and the file is NOT encrypted).
    bool        encryptionActive() const { return encryption_active_; }
    // The SQLCipher codec version string, or "" if the build has no codec.
    std::string cipherVersion() const { return cipher_version_; }

    // Derives a stable 256-bit hex key for this machine/install from `material`
    // (e.g. a per-install secret). Deterministic so the same install re-derives
    // the same key; not a substitute for an OS keystore but keeps the key off the
    // command line. Suitable as the `key` argument to open().
    static std::string deriveKey(const std::string& material);

    // Loads (or, on first run, creates) the per-install at-rest encryption key.
    // The key is a random 256-bit secret stored at `keyfile`, protected at rest
    // by the OS user keystore (Windows DPAPI / CryptProtectData, scoped to the
    // current user) so it is local-only and never hardcoded. Returns a 64-char
    // hex key suitable for open(); returns "" only if a keyfile cannot be
    // created (caller may then fall back to an unencrypted open).
    static std::string loadOrCreateKey(const std::string& keyfile);

    // True if the linked SQLite library has a working SQLCipher codec (probed at
    // runtime). When false, open(path,key) cannot cipher the file.
    static bool hasCodec();

    // Runs the canonical schema (idempotent) and records kSchemaVersion.
    bool migrate();

    // True if `table` already defines a column named `column` (PRAGMA table_info).
    // Used by migrate() to apply ALTER ADD COLUMN patches idempotently.
    bool hasColumn(const std::string& table, const std::string& column);

    // Parameterized write. Params bound positionally (?1..). Returns last rowid.
    int64_t exec(const std::string& sql, const std::vector<nlohmann::json>& params = {});

    // Parameterized read; invokes `fn` per row.
    void query(const std::string& sql,
               const std::vector<nlohmann::json>& params,
               const std::function<void(const Row&)>& fn);

    // Convenience key-value settings access (settings table).
    std::string getSetting(const std::string& key, const std::string& def = "");
    void        setSetting(const std::string& key, const std::string& value);
    bool        getBool(const std::string& key, bool def);

    std::mutex& mutex() { return mtx_; }   // for multi-statement transactions
    sqlite3*    raw() { return db_; }

private:
    // Returns the single-column text value of a one-row PRAGMA/SELECT, or def.
    std::string scalar(const std::string& sql, const std::string& def = "");

    // One-time in-place conversion of an existing plaintext DB at `path` into a
    // SQLCipher-encrypted DB keyed with `key` (preserves a .plaintext.bak).
    static bool migratePlaintextToEncrypted(const std::string& path,
                                            const std::string& key);

    sqlite3*    db_ = nullptr;
    std::mutex  mtx_;
    bool        encryption_active_ = false;
    std::string cipher_version_;
};

} // namespace polymath
