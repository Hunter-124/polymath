// ---------------------------------------------------------------------------
//  Persona bundle seeding / stock propagation
// ---------------------------------------------------------------------------
//
//  Unit-tests seedBundlesFrom() — the logic that provisions the shipped persona
//  bundles into the user's data dir and, on later runs, propagates stock
//  improvements WITHOUT clobbering the user's own edits. Drives the three paths
//  off a throwaway temp src/dest tree (no Paths, no app, no real assets):
//
//    1. missing bundle            -> copied wholesale
//    2. stock changed, unedited   -> persona.json refreshed (+ new siblings)
//    3. stock changed, user-edited-> left intact, new stock dropped as .new
//
//  plus idempotency (a no-op second run) and that a newly shipped persona shows
//  up on a populated install.
//
#include "bundle_seed.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <cassert>
#include <cstdio>

namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

const std::string kV1   = R"({"name":"X","v":1})";
const std::string kV2   = R"({"name":"X","v":2,"tools":["calculate"]})";
const std::string kEdit = R"({"name":"X","v":1,"note":"user tweaked this"})";

} // namespace

int main() {
    using polymath::seedBundlesFrom;

    const fs::path root = fs::temp_directory_path() / "pm_bundle_seed_test";
    std::error_code ec;
    fs::remove_all(root, ec);                 // start from a clean slate

    const fs::path src  = root / "src";
    const fs::path dest = root / "dest";

    // --- shipped source tree: two personas, one with a sibling avatar ---------
    writeFile(src / "alpha" / "persona.json", kV1);
    writeFile(src / "alpha" / "avatar.png", "PNGDATA");
    writeFile(src / "beta"  / "persona.json", kV1);

    // === 1) first run: empty dest -> both bundles copied wholesale ============
    {
        const int n = seedBundlesFrom(src, dest);
        assert(n == 2 && "first run should copy both bundles");
        assert(readFile(dest / "alpha" / "persona.json") == kV1);
        assert(readFile(dest / "beta"  / "persona.json") == kV1);
        assert(readFile(dest / "alpha" / "avatar.png") == "PNGDATA");
        assert(exists(dest / ".stock-manifest.json") && "manifest written");
    }
    std::puts("  [ok]   first-run copy");

    // === 2) stock bump, user untouched -> persona.json refreshed ==============
    // Also ship a NEW sibling file: it should be added on refresh.
    writeFile(src / "alpha" / "persona.json", kV2);
    writeFile(src / "alpha" / "extra.txt", "stock-extra");
    {
        const int n = seedBundlesFrom(src, dest);
        assert(n == 1 && "only alpha should refresh");
        assert(readFile(dest / "alpha" / "persona.json") == kV2 && "stock update propagated");
        assert(readFile(dest / "alpha" / "extra.txt") == "stock-extra" && "new sibling copied");
        assert(readFile(dest / "beta"  / "persona.json") == kV1 && "beta unchanged");
        assert(!exists(dest / "alpha" / "persona.json.new") && "no sidecar for refreshed bundle");
    }
    std::puts("  [ok]   stock refresh of unedited persona");

    // === 3) stock bump, but the user edited this persona -> preserve + .new ====
    writeFile(dest / "beta" / "persona.json", kEdit);   // user edits their copy
    writeFile(src  / "beta" / "persona.json", kV2);      // and the stock changes
    {
        const int n = seedBundlesFrom(src, dest);
        assert(n == 0 && "no bundle should be written: alpha unchanged, beta preserved");
        assert(readFile(dest / "beta" / "persona.json") == kEdit && "user edit preserved");
        assert(readFile(dest / "beta" / "persona.json.new") == kV2 && "new stock offered as .new");
    }
    std::puts("  [ok]   user-edited persona preserved, stock offered as .new");

    // === 4) idempotency: nothing changed -> a second run is a no-op ===========
    {
        const int n = seedBundlesFrom(src, dest);
        assert(n == 0 && "steady-state run writes nothing");
        assert(readFile(dest / "alpha" / "persona.json") == kV2);
        assert(readFile(dest / "beta"  / "persona.json") == kEdit);
    }
    std::puts("  [ok]   idempotent steady state");

    // === 5) a newly shipped persona appears on a populated install ============
    writeFile(src / "gamma" / "persona.json", kV1);
    {
        const int n = seedBundlesFrom(src, dest);
        assert(n == 1 && "the new persona should be copied");
        assert(readFile(dest / "gamma" / "persona.json") == kV1);
    }
    std::puts("  [ok]   newly shipped persona seeded onto populated install");

    fs::remove_all(root, ec);
    std::puts("[personality] OK");
    return 0;
}
