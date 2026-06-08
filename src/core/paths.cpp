#include "paths.h"

namespace polymath {

Paths& Paths::instance() { static Paths p; return p; }

void Paths::setRoot(const std::filesystem::path& root) { root_ = root; }

void Paths::ensureLayout() const {
    std::error_code ec;
    for (const auto& d : { models(), personalities(), media(), vectors(), documents(), logs() })
        std::filesystem::create_directories(d, ec);
}

} // namespace polymath
