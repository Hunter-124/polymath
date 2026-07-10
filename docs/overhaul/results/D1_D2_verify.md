# D1 / D2 verify results (2026-07-10)

## D1 build (build/cpu, VS Release)
- Polymath.exe: OK
- capture_views.exe: OK
- Captures populated: 13/13 OK
- Captures empty: 13/13 OK
- Minor QML: Cameras image provider missing in capture (expected); AgentSessions radius fixed

## D2 ctest -C Release
14/14 passed:
core, tools, audio, agent, vision, inference, memory, privacy, integration, ui, j_phase2, harness, skills, sessions

## Fixes landed with this node
- tests/test_audio_e2e.cpp: include capture.h; ring fill without false drops
- qml/AgentSessionsView.qml: radius Style.radius (undefined parent.radius)
