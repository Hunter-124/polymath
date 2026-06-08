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

    // Opens (creating if needed) and applies the schema. key != "" enables
    // SQLCipher at-rest encryption. Returns false on failure.
    bool open(const std::string& path, const std::string& key = "");
    void close();

    // Runs the canonical schema (idempotent) and records kSchemaVersion.
    bool migrate();

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
    sqlite3*   db_ = nullptr;
    std::mutex mtx_;
};

} // namespace polymath
