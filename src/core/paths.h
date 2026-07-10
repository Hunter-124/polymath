#pragma once
//
// Canonical on-disk locations.  The app root is resolved once at startup
// (portable: a `data/` folder next to the exe; or %LOCALAPPDATA%/Polymath when
// installed).  All other paths derive from it so services never hard-code dirs.
//
#include <filesystem>
#include <string>

namespace polymath {

class Paths {
public:
    static Paths& instance();

    void   setRoot(const std::filesystem::path& root);
    const std::filesystem::path& root() const { return root_; }

    std::filesystem::path db()            const { return root_ / "polymath.db"; }
    std::filesystem::path models()        const { return root_ / "models"; }
    std::filesystem::path personalities() const { return root_ / "personalities"; }
    std::filesystem::path media()         const { return root_ / "media"; }
    std::filesystem::path vectors()       const { return root_ / "vectors"; }
    std::filesystem::path documents()     const { return root_ / "documents"; }
    std::filesystem::path logs()          const { return root_ / "logs"; }

    void ensureLayout() const;   // create the directory tree if missing

private:
    Paths() = default;
    std::filesystem::path root_;
};

} // namespace polymath
