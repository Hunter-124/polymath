# agent — tool registry + agentic loop + builtin tools.
# Wave-2 agent adds tools/*.cpp (web, docs, print, reminders, memory, camera).
qt_add_library(pm_agent STATIC
    agent_runtime.cpp
    tool_registry.cpp
    tools/shopping_tool.cpp
)
target_include_directories(pm_agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_agent PUBLIC pm_core pm_inference pm_scheduler pm_memory
    PRIVATE Qt6::Network Qt6::Gui)   # Network: web tools; Gui: QPrinter for print tools
