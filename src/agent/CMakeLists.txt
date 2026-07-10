# agent — tool registry + agentic loop + builtin tools.
# The agent runtime drives the GBNF-constrained tool-calling loop; tools/* are
# the builtin ITools (web, docs, print, shopping, reminders, memory, camera, queue).

# QPrinter (print tools) lives in Qt6::PrintSupport, which the top-level
# find_package does not request; pull it in here (additive, module-local).
find_package(Qt6 REQUIRED COMPONENTS PrintSupport)

qt_add_library(pm_agent STATIC
    agent_runtime.cpp
    agent_loop.cpp
    tool_registry.cpp
    turn_collector.cpp
    persona.cpp
    tools/register_tools.cpp
    tools/tool_support.cpp
    tools/docx_writer.cpp
    tools/shopping_tool.cpp
    tools/web_search.cpp
    tools/fetch_page.cpp
    tools/browser_drive.cpp
    tools/documents.cpp
    tools/print.cpp
    tools/reminders.cpp
    tools/memory_tools.cpp
    tools/camera_tools.cpp
    tools/queue_tool.cpp
)
target_include_directories(pm_agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_agent PUBLIC pm_core pm_inference pm_scheduler pm_memory
    PRIVATE Qt6::Network Qt6::Gui Qt6::PrintSupport)   # Network: web tools; Gui/PrintSupport: print tools
