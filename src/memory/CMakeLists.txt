# memory — SQLite store + hnswlib vector index + daily summarizer.
qt_add_library(pm_memory STATIC
    memory_service.cpp
)
target_include_directories(pm_memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_memory PUBLIC pm_core pm_inference PRIVATE hnswlib)
