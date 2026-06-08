# personality — hot-loadable persona bundle manager.
qt_add_library(pm_personality STATIC
    personality_manager.cpp
)
target_include_directories(pm_personality PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_personality PUBLIC pm_core)
