# inference — tiered llama.cpp model manager. Wave-1 agent adds llama_backend.cpp,
# vram_budget.cpp, grammar.cpp and links the `llama` target from third_party.
qt_add_library(pm_inference STATIC
    inference_manager.cpp
)
target_include_directories(pm_inference PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_inference PUBLIC pm_core)
if(TARGET llama)
    target_link_libraries(pm_inference PRIVATE llama)
    target_compile_definitions(pm_inference PRIVATE POLYMATH_HAVE_LLAMA=1)
endif()
