# scheduler — deep-work queue + proactive engine + idle detector.
qt_add_library(pm_scheduler STATIC
    task_scheduler.cpp
    proactive_engine.cpp
    idle_detector.cpp
    scheduler_util.cpp
)
target_include_directories(pm_scheduler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# A3: tool dispatch needs agent headers; memory service for daily_summary.
target_include_directories(pm_scheduler PRIVATE
    ${CMAKE_SOURCE_DIR}/src/agent
    ${CMAKE_SOURCE_DIR}/src/memory)
target_link_libraries(pm_scheduler PUBLIC pm_core pm_inference pm_memory)
