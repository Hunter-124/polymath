# Minimal smoke/unit tests. Each Wave agent adds a test for its module here.
add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE pm_core)
add_test(NAME core COMMAND test_core)

add_executable(test_tools test_tools.cpp)
target_link_libraries(test_tools PRIVATE pm_core pm_agent)
add_test(NAME tools COMMAND test_tools)

# === append-only block: Wave-1 card tests ===================================
# Card D — tiered inference + scheduler. Budget math + grammar always run; the
# real-model load/unload and full Fast->Heavy->drain->Fast cycle gate on a model
# being present and the POLYMATH_E2E_FULL env var (see test header).
add_executable(test_inference_e2e test_inference_e2e.cpp)
target_link_libraries(test_inference_e2e PRIVATE pm_core pm_inference pm_scheduler Qt6::Core)
add_test(NAME inference COMMAND test_inference_e2e)
