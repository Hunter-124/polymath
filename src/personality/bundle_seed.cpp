#include "bundle_seed.h"
#include "paths.h"
#include "logging.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QByteArray>
#include <QDir>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// First-run seeding of the starter persona bundles.  We can't rely on CMake
// installing the assets next to the exe (the top-level build script is frozen),
// so we resolve the source location at runtime from a prioritized candidate
// list.  POLYMATH_ASSETS_DIR is baked in by this module's CMakeLists and points
// at the in-tree assets/personalities/, which makes dev builds "just work"; the
// runtime-relative fallbacks cover a packaged/installed layout.

namespace polymath {

namespace fs = std::filesystem;

namespace {

// Does `dir` contain at least one "<name>/persona.json" bundle?
bool hasAnyBundle(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        std::error_code fec;
        if (entry.is_directory(fec) && fs::exists(entry.path() / "persona.json", fec))
            return true;
    }
    return false;
}

// Ordered list of places the shipped bundles might live.  The exe lives under
// .../bin, so the source tree sits a couple levels up in a dev build; an
// installer would drop assets beside the exe.  Duplicates are harmless — the
// first existing, bundle-bearing directory wins.
std::vector<fs::path> candidateRoots() {
    std::vector<fs::path> out;

#ifdef POLYMATH_ASSETS_DIR
    out.emplace_back(fs::path(POLYMATH_ASSETS_DIR));
#endif

    // Runtime-relative fallbacks (packaged layouts / running from the build dir).
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const fs::path base = fs::path(QDir(appDir).absolutePath().toStdWString());
        out.push_back(base / "assets" / "personalities");
        out.push_back(base / ".." / "assets" / "personalities");
        out.push_back(base / ".." / ".." / "assets" / "personalities");
        out.push_back(base / ".." / ".." / ".." / "assets" / "personalities");
    }

    // Beside / above the data root (portable installs keep data next to assets).
    const fs::path root = Paths::instance().root();
    if (!root.empty()) {
        out.push_back(root / "assets" / "personalities");
        out.push_back(root.parent_path() / "assets" / "personalities");
    }

    return out;
}

} // namespace

fs::path locateStarterBundles() {
    std::error_code ec;
    for (const auto& cand : candidateRoots()) {
        if (cand.empty()) continue;
        fs::path norm = fs::weakly_canonical(cand, ec);
        const fs::path& probe = ec ? cand : norm;
        ec.clear();
        if (hasAnyBundle(probe)) {
            PM_DEBUG("personality: starter bundles located at {}", probe.string());
            return probe;
        }
    }
    return {};
}

namespace {
using nlohmann::json;

// Whole-file bytes, or "" if unreadable.
std::string readBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string sha256Hex(const std::string& bytes) {
    const QByteArray hex = QCryptographicHash::hash(
        QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())),
        QCryptographicHash::Sha256).toHex();
    return std::string(hex.constData(), static_cast<size_t>(hex.size()));
}

// SHA-256 of a persona.json (hex), or "" if it cannot be read.
std::string personaHash(const fs::path& persona_json) {
    std::error_code ec;
    if (!fs::exists(persona_json, ec)) return {};
    return sha256Hex(readBytes(persona_json));
}

json loadManifest(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return json::object();
    try {
        json j;
        in >> j;
        if (j.is_object()) return j;
    } catch (...) {}
    return json::object();
}

void saveManifest(const fs::path& path, const json& j) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out) out << j.dump(2) << '\n';
}

// Copy stock files from `srcBundle` into `dstBundle` that don't exist yet (e.g.
// a newly shipped avatar). Never overwrites existing files. persona.json is
// handled explicitly by the caller.
void copyMissingSiblings(const fs::path& srcBundle, const fs::path& dstBundle) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(srcBundle, ec)) {
        std::error_code fec;
        if (!e.is_regular_file(fec)) continue;
        if (e.path().filename() == "persona.json") continue;
        const fs::path target = dstBundle / e.path().filename();
        if (fs::exists(target, fec)) continue;
        std::error_code cec;
        fs::copy_file(e.path(), target, cec);
    }
}

std::string recordedHashOf(const json& manifest, const std::string& name) {
    auto it = manifest.find(name);
    if (it != manifest.end() && it->is_string()) return it->get<std::string>();
    return {};
}

} // namespace

int seedBundlesFrom(const fs::path& src, const fs::path& dest) {
    std::error_code ec;
    if (src.empty() || !fs::is_directory(src, ec)) {
        PM_WARN("personality: no source bundle dir to seed from ({})", src.string());
        return 0;
    }
    fs::create_directories(dest, ec);

    const fs::path manifestPath = dest / ".stock-manifest.json";
    json manifest = loadManifest(manifestPath);

    int copied = 0, refreshed = 0, preserved = 0;
    for (auto& entry : fs::directory_iterator(src, ec)) {
        std::error_code fec;
        if (!entry.is_directory(fec)) continue;
        const fs::path srcPersona = entry.path() / "persona.json";
        if (!fs::exists(srcPersona, fec)) continue;

        const std::string name = entry.path().filename().string();
        const fs::path target     = dest / name;
        const fs::path dstPersona = target / "persona.json";
        const std::string shippedHash = personaHash(srcPersona);

        // (1) New bundle — copy wholesale (first run / freshly shipped persona).
        if (!fs::exists(target, fec)) {
            std::error_code cec;
            fs::copy(entry.path(), target, fs::copy_options::recursive, cec);
            if (cec) {
                PM_WARN("personality: failed to copy bundle {} -> {}: {}",
                        entry.path().string(), target.string(), cec.message());
                continue;
            }
            manifest[name] = shippedHash;
            ++copied;
            continue;
        }

        const std::string installedHash = personaHash(dstPersona);
        const std::string recordedHash  = recordedHashOf(manifest, name);

        // Already identical to the shipped stock — make sure a baseline is on
        // record so a *future* stock bump can refresh it.
        if (installedHash == shippedHash) {
            manifest[name] = shippedHash;
            continue;
        }

        // (2) Untouched by the user since our last write, and the stock changed
        // — refresh persona.json and add any newly shipped sibling files. This
        // is what lets stock persona improvements reach already-seeded installs.
        if (!recordedHash.empty() && installedHash == recordedHash) {
            std::error_code cec;
            fs::copy_file(srcPersona, dstPersona,
                          fs::copy_options::overwrite_existing, cec);
            if (cec) {
                PM_WARN("personality: failed to refresh persona '{}': {}",
                        name, cec.message());
                continue;
            }
            copyMissingSiblings(entry.path(), target);
            manifest[name] = shippedHash;
            ++refreshed;
            continue;
        }

        // (3) User-edited (or predates the manifest) and differs from stock —
        // never clobber. Drop the new stock beside it as persona.json.new so the
        // update is discoverable. Leave the manifest baseline alone: we must not
        // record the user's content as "stock", or a later bump would overwrite.
        const fs::path sidecar = target / "persona.json.new";
        std::error_code cec;
        fs::copy_file(srcPersona, sidecar, fs::copy_options::overwrite_existing, cec);
        ++preserved;
    }

    saveManifest(manifestPath, manifest);

    if (copied || refreshed || preserved)
        PM_INFO("personality: bundles {} -> {} (copied {}, refreshed {}, "
                "preserved {} user-edited)",
                src.string(), dest.string(), copied, refreshed, preserved);
    return copied + refreshed;
}

int seedStarterBundles(const fs::path& dest) {
    const fs::path src = locateStarterBundles();
    if (src.empty()) {
        PM_WARN("personality: no starter bundles found to seed into {}", dest.string());
        return 0;
    }
    return seedBundlesFrom(src, dest);
}

} // namespace polymath
