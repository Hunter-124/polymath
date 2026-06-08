# scheduler — deep-work queue + proactive engine + idle detector.
qt_add_library(pm_scheduler STATIC
    scheduler_stub.cpp
)
target_include_directories(pm_scheduler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_scheduler PUBLIC pm_core pm_inference)
