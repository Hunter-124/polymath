#include "logging.h"
#include <vector>
#include <filesystem>

namespace polymath::logging {

void init(const std::string& log_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(log_dir, ec);

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        (fs::path(log_dir) / "polymath.log").string(), 5 * 1024 * 1024, 3));

    auto logger = std::make_shared<spdlog::logger>("polymath", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::debug);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::warn);
}

} // namespace polymath::logging
