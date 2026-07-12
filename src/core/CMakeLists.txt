# core — shared-contract layer. Everything else links this.
qt_add_library(pm_core STATIC
    event_bus.cpp
    database.cpp
    logging.cpp
    paths.cpp
    config.cpp
    retention.cpp
    activity_log.cpp
    safety_policy.cpp
    single_instance.cpp
)

target_include_directories(pm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(pm_core PUBLIC
    Qt6::Core
    nlohmann_json::nlohmann_json
    unofficial::sqlite3::sqlite3
    spdlog::spdlog
    fmt::fmt
)

# Public headers (the contract): types.h event_bus.h i_model_backend.h i_tool.h
#                                schema.h database.h paths.h config.h logging.h
#                                single_instance.h
