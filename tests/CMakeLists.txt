# Minimal smoke/unit tests. Each Wave agent adds a test for its module here.
add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE pm_core)
add_test(NAME core COMMAND test_core)

add_executable(test_tools test_tools.cpp)
target_link_libraries(test_tools PRIVATE pm_core pm_agent)
add_test(NAME tools COMMAND test_tools)

# ---------------------------------------------------------------------------
# Append-only block: each Wave agent appends its own test registration below.
# Keep additions self-contained (one add_executable + target_link_libraries +
# add_test per card) so merges are trivial.
# ---------------------------------------------------------------------------

# Wave 1 · Card A — voice loop end-to-end (audio).
add_executable(test_audio_e2e test_audio_e2e.cpp)
target_link_libraries(test_audio_e2e PRIVATE pm_core pm_audio)
add_test(NAME audio COMMAND test_audio_e2e)

# Wave 1 · Card B — agent tools end-to-end: all 16 builtin tools' invoke()
# effects asserted directly + one GBNF LLM-driven tool round-trip (skipped if no
# Fast model is on disk). Drives the registry, persona/grammar, and dispatch.
add_executable(test_agent_e2e test_agent_e2e.cpp)
target_link_libraries(test_agent_e2e PRIVATE pm_core pm_agent pm_inference pm_scheduler)
add_test(NAME agent COMMAND test_agent_e2e)
# The LLM round-trip cold-loads a model on CPU and runs a multi-step tool loop.
set_tests_properties(agent PROPERTIES TIMEOUT 600)

# Wave 1 · Card C — vision end-to-end: person/motion/face on recorded fixtures +
# Finder plumbing. ONNX runs on CPU. Fixtures ship under tests/fixtures/vision/;
# the ONNX/GGUF models are looked up in the deployed data/models tree next to
# Polymath.exe (junctioned to the shared copy in a fresh worktree).
add_executable(test_vision_e2e test_vision_e2e.cpp)
target_link_libraries(test_vision_e2e PRIVATE
    pm_vision pm_inference pm_core
    Qt6::Core ${OpenCV_LIBS} onnxruntime::onnxruntime)
target_include_directories(test_vision_e2e PRIVATE ${OpenCV_INCLUDE_DIRS})
target_compile_definitions(test_vision_e2e PRIVATE
    PM_VISION_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vision"
    PM_VISION_MODELS="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/data/models")
add_test(NAME vision COMMAND test_vision_e2e)

# Wave 1 · Card D — tiered inference + scheduler. Budget math + grammar always
# run; the real-model load/unload and full Fast->Heavy->drain->Fast cycle gate on
# a model being present and the POLYMATH_E2E_FULL env var (see test header).
add_executable(test_inference_e2e test_inference_e2e.cpp)
target_link_libraries(test_inference_e2e PRIVATE pm_core pm_inference pm_scheduler Qt6::Core)
add_test(NAME inference COMMAND test_inference_e2e)

# Wave 1 · Card E — memory end-to-end: recall / persistence / summarizer /
# retention. Links pm_memory (store + vector index + summarizer) and pm_inference
# (real EmbeddingGemma embeddings); needs Qt Core for the EventBus/metatype machinery.
add_executable(test_memory_e2e test_memory_e2e.cpp)
target_link_libraries(test_memory_e2e PRIVATE pm_core pm_inference pm_memory Qt6::Core)
add_test(NAME memory COMMAND test_memory_e2e)

# Wave 2 · Card G — privacy / persistence / at-rest security end-to-end:
# master kill-switch gating, per-category retention sweep, keyed (SQLCipher-aware)
# DB open round-trip, and the durable web/tool activity log. Pure core data layer,
# so it links only pm_core (Qt Core for the metatype machinery pm_core pulls in).
add_executable(test_privacy_e2e test_privacy_e2e.cpp)
target_link_libraries(test_privacy_e2e PRIVATE pm_core Qt6::Core)
add_test(NAME privacy COMMAND test_privacy_e2e)

# Wave 2 · Card H — integration: headless drive-the-app harness + cross-service
# flows. Boots the WHOLE backend offscreen (AppController without the GUI, every
# service on its own QThread, real EventBus) via tests/test_harness.h, then drives
# flows that span module boundaries:
#   * Utterance -> AgentRuntime -> transcripts(DB) + SpeakRequest(bus)
#   * setPrivacy -> privacyChanged(bus) + settings(DB)
#   * addShoppingItem -> shopping_items(DB)
#   * queue_deep_task -> tasks(DB,'queued') + taskUpdated(bus); heavy drain opt-in.
# Links pm_app (the AppController facade) so the harness uses the app's own
# construction path. Default suite is model-free + deterministic; the heavy
# scheduler-drain half is gated on POLYMATH_E2E_FULL=1 (+ a model on disk), so
# this stays green on a CI machine without the ~28 GB models. QT_QPA_PLATFORM is
# forced to offscreen so no display is required.
add_executable(test_integration_e2e test_integration_e2e.cpp test_harness.h)
target_link_libraries(test_integration_e2e PRIVATE pm_app Qt6::Core)
add_test(NAME integration COMMAND test_integration_e2e)
set_tests_properties(integration PROPERTIES
    TIMEOUT 300
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Wave 2 · Card F — UI render end-to-end: loads the Main shell + all 9 views
# offscreen against a stub `app`/list-models and asserts each instantiates with
# ZERO QML errors (catches role-name drift, bad imports, missing bindings before
# they ship as blank panels). Links pm_ui for the embedded Polymath QML module
# (built NO_PLUGIN, so types resolve from resources). Offscreen, no backend.
add_executable(test_ui_e2e test_ui_e2e.cpp)
set_target_properties(test_ui_e2e PROPERTIES AUTOMOC ON)
target_link_libraries(test_ui_e2e PRIVATE pm_ui Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Gui)
add_test(NAME ui COMMAND test_ui_e2e)
set_tests_properties(ui PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Wave 3 · Card J — Phase 2: ESP32-CAM ingest (simulated) + browser_drive (CDP).
#   A) An in-process QTcpServer streams multipart/x-mixed-replace JPEG frames in
#      the exact framing the shipped esp32cam.ino firmware emits; a real
#      CameraWorker ingests it (MJPEG over HTTP via OpenCV/FFMPEG) and we assert
#      the tile-frame + motion-Detection + events-row path, plus auto-reconnect.
#   B) browser_drive: verifies the CDP RFC6455 WebSocket framing round-trip
#      (no Chrome needed) and, when Chrome is installed, a live navigate + type +
#      extract round-trip against a local file:// page (auto-skips if absent).
# Links pm_vision (CameraWorker) + pm_agent (BrowserDriveTool + cdpws helpers) +
# OpenCV (encode the MJPEG frames) + Qt Network (the in-process MJPEG server).
add_executable(test_j_phase2_e2e test_j_phase2_e2e.cpp)
target_link_libraries(test_j_phase2_e2e PRIVATE
    pm_vision pm_agent pm_core
    Qt6::Core Qt6::Network ${OpenCV_LIBS} onnxruntime::onnxruntime)
target_include_directories(test_j_phase2_e2e PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/agent/tools)
target_compile_definitions(test_j_phase2_e2e PRIVATE
    PM_VISION_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vision")
add_test(NAME j_phase2 COMMAND test_j_phase2_e2e)
set_tests_properties(j_phase2 PROPERTIES
    TIMEOUT 240
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Wave 3 · Card K — device-fabric unit tests: FabricService ingest* paths +
# DeviceRegistry CRUD. No MQTT, no network — all ingest* called directly.
# Asserts edge_devices/cameras upsert (announce), measurements in_range flag,
# events clip_url/confidence/device_id, Detection + InstrumentReading signals,
# and DeviceRegistry list/get/inRange edge cases. Links pm_fabric (the new
# fabric module) + pm_core (Database/EventBus) + Qt::Core (EventBus metatypes).
add_executable(test_fabric test_fabric.cpp)
target_link_libraries(test_fabric PRIVATE pm_fabric pm_core Qt6::Core)
add_test(NAME fabric COMMAND test_fabric)
set_tests_properties(fabric PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Wave 3 · Card K — instrument tool unit tests: ReadInstrumentTool and
# RecordMeasurementTool against a temp DB. Asserts missing-id/no-reading
# failures, latest-reading retrieval, in-range vs out-of-range rows + ok flag,
# and ad-hoc (no instrument row) always-in-range behaviour.
# Links pm_agent (instrument_tool.cpp) + pm_core + Qt::Core.
add_executable(test_instruments test_instruments.cpp)
target_link_libraries(test_instruments PRIVATE pm_agent pm_core Qt6::Core)
target_include_directories(test_instruments PRIVATE ${CMAKE_SOURCE_DIR}/src/agent/tools)
add_test(NAME instruments COMMAND test_instruments)
set_tests_properties(instruments PROPERTIES TIMEOUT 60)

# Wave 3 · Card K — lab-session state-machine tests: drives the full lifecycle
# start_lab_session -> next_lab_step -> verify_lab_step (in-range AND
# out-of-range + retry) -> finish_lab_session. Asserts DB row transitions
# (lab_sessions, lab_session_steps, measurements), LabStepEvent signals, and
# the done/report_doc_id result from finish. Links pm_agent + pm_core + Qt::Core
# (EventBus metatypes + DirectConnection signal delivery).
add_executable(test_lab_session test_lab_session.cpp)
target_link_libraries(test_lab_session PRIVATE pm_agent pm_core Qt6::Core)
target_include_directories(test_lab_session PRIVATE ${CMAKE_SOURCE_DIR}/src/agent/tools)
add_test(NAME lab_session COMMAND test_lab_session)
set_tests_properties(lab_session PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
