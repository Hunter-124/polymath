#include "database.h"
#include "logging.h"
#include <sqlite3.h>
#include <cstdint>

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

bool Database::migrate() {
    std::lock_guard lk(mtx_);
    char* err = nullptr;
    if (sqlite3_exec(db_, kSchemaSQL, nullptr, nullptr, &err) != SQLITE_OK) {
        PM_ERROR("schema migrate failed: {}", err ? err : "?");
        sqlite3_free(err);
        return false;
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
