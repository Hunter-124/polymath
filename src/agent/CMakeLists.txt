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
    skills/skill.cpp
    skills/skill_registry.cpp
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
    tools/skill_tools.cpp
    # Named uicontrol_tool (not ui_control_tool) so AUTOUIC does not treat
    # #include "ui_*.h" as a Qt Designer form header.
    tools/uicontrol_tool.cpp
    tools/agent_session_tools.cpp
)
target_include_directories(pm_agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Qt Core: QFileSystemWatcher / QTimer for SkillRegistry hot-reload.
# Network: web tools; Gui/PrintSupport: print tools.
# agent_* tools talk to AgentSessionService via QObject/QMetaObject (no hard
# link to pm_sessions — that target is added after pm_agent in the root CMake).
target_link_libraries(pm_agent PUBLIC pm_core pm_inference pm_scheduler pm_memory
    PRIVATE Qt6::Core Qt6::Network Qt6::Gui Qt6::PrintSupport)

# Starter skill bundles live in the source tree under data/skills/. SkillRegistry
# seeds an empty user skills dir from this path on first load (dev builds).
target_compile_definitions(pm_agent PRIVATE
    POLYMATH_SKILLS_DIR="${CMAKE_SOURCE_DIR}/data/skills"
)
