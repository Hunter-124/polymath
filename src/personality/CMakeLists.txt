# personality — hot-loadable persona bundle manager.
qt_add_library(pm_personality STATIC
    personality_manager.cpp
    bundle_seed.cpp
)
target_include_directories(pm_personality PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# pm_core already brings in Qt6::Core transitively, but QFileSystemWatcher/QDir/
# QTimer are used directly here — link it explicitly so this target is self-
# contained.
target_link_libraries(pm_personality PUBLIC pm_core Qt6::Core)

# Bake in the source-tree location of the shipped starter bundles so first-run
# seeding works in a dev build.  At runtime bundle_seed also probes paths
# relative to the executable, which covers packaged/installed layouts where this
# compile-time path won't exist.
target_compile_definitions(pm_personality PRIVATE
    POLYMATH_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets/personalities"
)
