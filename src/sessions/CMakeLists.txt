# sessions — external AI CLI agent sessions (Claude Code / Codex / PTY).
# Spec: docs/overhaul/05_AGENT_SESSIONS.md. Linked into pm_app; service on own QThread.
qt_add_library(pm_sessions STATIC
    # Header listed so AUTOMOC generates IAgentProvider meta-object (Q_OBJECT).
    i_agent_provider.h
    agent_session_service.cpp
    providers/claude_stream_parse.cpp
    providers/claude_code_provider.cpp
    providers/codex_provider.cpp
    providers/generic_pty_provider.cpp
)
target_include_directories(pm_sessions PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/providers
)
target_link_libraries(pm_sessions PUBLIC
    pm_core
    Qt6::Core
)
