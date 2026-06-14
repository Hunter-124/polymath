#include "bundle_seed.h"
#include "paths.h"
#include "logging.h"

#include <QCoreApplication>
#include <QDir>

#include <filesystem>
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

int seedStarterBundles(const fs::path& dest) {
    std::error_code ec;

    const fs::path src = locateStarterBundles();
    if (src.empty()) {
        PM_WARN("personality: no starter bundles found to seed into {}", dest.string());
        return 0;
    }

    fs::create_directories(dest, ec);

    int copied = 0;
    for (auto& entry : fs::directory_iterator(src, ec)) {
        std::error_code fec;
        if (!entry.is_directory(fec)) continue;
        if (!fs::exists(entry.path() / "persona.json", fec)) continue;

        const fs::path target = dest / entry.path().filename();
        // Add shipped bundles that are MISSING; never overwrite the user's existing
        // ones — so manual edits survive AND new shipped personas (e.g. Operator)
        // appear after an update, not only on a clean first run.
        if (fs::exists(target, fec)) continue;
        std::error_code cec;
        fs::copy(entry.path(), target, fs::copy_options::recursive, cec);
        if (cec) {
            PM_WARN("personality: failed to copy bundle {} -> {}: {}",
                    entry.path().string(), target.string(), cec.message());
            continue;
        }
        ++copied;
    }

    if (copied > 0)
        PM_INFO("personality: seeded {} starter bundle(s) from {} into {}",
                copied, src.string(), dest.string());
    return copied;
}

} // namespace polymath
