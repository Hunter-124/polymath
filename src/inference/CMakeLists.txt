# inference — tiered llama.cpp model manager (Fast resident, Heavy/Vision/
# Embedding on demand) arbitrating a ~8 GB VRAM budget. Wraps llama.cpp via
# core::IModelBackend and links the `llama` target from third_party. CUDA is
# only needed for the VRAM query (cudaMemGetInfo) and is optional.
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
    # Multimodal (VLM) projector helper. Newer llama.cpp exposes the `mtmd`
    # target under tools/mtmd; link + flag it when available so describeImage()
    # compiles the real CLIP/mmproj path.
    if(TARGET mtmd)
        target_link_libraries(pm_inference PRIVATE mtmd)
        target_compile_definitions(pm_inference PRIVATE POLYMATH_HAVE_MTMD=1)
    endif()
endif()

# CUDA runtime — used solely by vram_budget.cpp for cudaMemGetInfo. The toggle
# POLYMATH_USE_CUDA is forwarded from the top-level build (and also enables
# GGML_CUDA for llama.cpp in third_party). Falls back to a conservative budget
# when off.
if(POLYMATH_USE_CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        target_link_libraries(pm_inference PRIVATE CUDA::cudart)
        target_compile_definitions(pm_inference PRIVATE POLYMATH_USE_CUDA=1)
    else()
        message(WARNING "POLYMATH_USE_CUDA set but CUDAToolkit not found; "
                        "VRAM query will use the conservative default.")
    endif()
endif()
