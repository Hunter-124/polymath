#include "database.h"
#include "logging.h"
#include <sqlite3.h>

namespace polymath {

int64_t     Row::i64(int c)    const { return sqlite3_column_int64(stmt_, c); }
double      Row::dbl(int c)    const { return sqlite3_column_double(stmt_, c); }
bool        Row::isNull(int c) const { return sqlite3_column_type(stmt_, c) == SQLITE_NULL; }
std::string Row::text(int c)   const {
    auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, c));
    return p ? std::string(p) : std::string();
}

Database::~Database() { close(); }

bool Database::open(const std::string& path, const std::string& key) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        PM_ERROR("sqlite open failed: {}", sqlite3_errmsg(db_));
        return false;
    }
    if (!key.empty()) {
        // SQLCipher: PRAGMA key must run before any other access.
        std::string pragma = "PRAGMA key = '" + key + "';";
        sqlite3_exec(db_, pragma.c_str(), nullptr, nullptr, nullptr);
    }
    sqlite3_busy_timeout(db_, 5000);
    return migrate();
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
