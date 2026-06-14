# inference — tiered llama.cpp model manager (Fast resident, Heavy/Vision/
# Embedding on demand) arbitrating a ~8 GB VRAM budget. Wraps llama.cpp via
# core::IModelBackend and links the `llama` target from third_party. GPU memory
# is queried at runtime through ggml's device registry (vram_budget.cpp), so this
# module no longer links cudart directly — the CUDA backend is just an optional
# ggml backend (static, or a runtime ggml-cuda.dll).
qt_add_library(pm_inference STATIC
    inference_manager.cpp
    llama_backend.cpp
    vram_budget.cpp
    grammar.cpp
)
target_include_directories(pm_inference PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_inference PUBLIC pm_core)

# llama.cpp (LLM + VLM + embeddings). Vendored as a git submodule and built by
# third_party/CMakeLists.txt. Guarded so the scaffold still configures without it.
if(TARGET llama)
    target_link_libraries(pm_inference PRIVATE llama)
    target_compile_definitions(pm_inference PRIVATE POLYMATH_HAVE_LLAMA=1)
    # PUBLIC marker so downstream consumers (e.g. the inference e2e test) can tell
    # the real engine is compiled in WITHOUT inheriting llama's headers/flags.
    target_compile_definitions(pm_inference PUBLIC POLYMATH_INFERENCE_HAS_LLAMA=1)
    # Multimodal (VLM) projector helper. Newer llama.cpp exposes the `mtmd`
    # target under tools/mtmd; link + flag it when available so describeImage()
    # compiles the real CLIP/mmproj path.
    if(TARGET mtmd)
        target_link_libraries(pm_inference PRIVATE mtmd)
        target_compile_definitions(pm_inference PRIVATE POLYMATH_HAVE_MTMD=1)
    endif()
endif()
