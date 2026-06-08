#pragma once
//
// Thin spdlog wrapper.  Use PM_INFO / PM_WARN / PM_ERROR / PM_DEBUG.
// Each subsystem should call logging::init() once at startup is NOT required —
// the default logger is created lazily on first use.
//
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>

namespace polymath::logging {

void init(const std::string& log_dir);   // sets up console + rotating file sinks

} // namespace polymath::logging

#define PM_TRACE(...) spdlog::trace(__VA_ARGS__)
#define PM_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define PM_INFO(...)  spdlog::info(__VA_ARGS__)
#define PM_WARN(...)  spdlog::warn(__VA_ARGS__)
#define PM_ERROR(...) spdlog::error(__VA_ARGS__)
