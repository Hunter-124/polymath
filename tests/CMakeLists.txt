# Minimal smoke/unit tests. Each Wave agent adds a test for its module here.
add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE pm_core)
add_test(NAME core COMMAND test_core)

add_executable(test_tools test_tools.cpp)
target_link_libraries(test_tools PRIVATE pm_core pm_agent)
add_test(NAME tools COMMAND test_tools)

# --- append-only block: one test per Wave card -----------------------------
# Card E (memory): end-to-end recall / persistence / summarizer / retention.
# Links pm_memory (store + vector index + summarizer) and pm_inference (real
# EmbeddingGemma embeddings); needs Qt Core for the EventBus/metatype machinery.
add_executable(test_memory_e2e test_memory_e2e.cpp)
target_link_libraries(test_memory_e2e PRIVATE pm_core pm_inference pm_memory Qt6::Core)
add_test(NAME memory COMMAND test_memory_e2e)
