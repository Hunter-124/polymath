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
