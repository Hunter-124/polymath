// Smoke test for the core data layer: open an on-disk temp DB, apply the
// schema, round-trip settings and a shopping item.
#include "database.h"
#include "config.h"
#undef NDEBUG   // keep assert() active even in Release (otherwise the test is a no-op)
#include <cassert>
#include <cstdio>
#include <filesystem>

using namespace polymath;

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "polymath_test_core.db";
    std::filesystem::remove(tmp);

    Database db;
    assert(db.open(tmp.string()));

    // settings round-trip + config defaults
    Config cfg(db);
    cfg.seedDefaults();
    assert(cfg.getBool(keys::MicEnabled) == true);   // privacy default ON
    db.setSetting("k", "v");
    assert(db.getSetting("k") == "v");

    // shopping insert + query
    db.exec("INSERT INTO shopping_items(item,created_at) VALUES(?1,?2)", {"milk", 123});
    int count = 0;
    db.query("SELECT item FROM shopping_items WHERE item=?1", {"milk"},
             [&](const Row& r) { assert(r.text(0) == "milk"); ++count; });
    assert(count == 1);

    db.close();
    std::filesystem::remove(tmp);
    std::puts("test_core: OK");
    return 0;
}
