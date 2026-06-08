# Vendored native sub-engines.  Each is a git submodule (see ../.gitmodules).
# We forward the CUDA option down and build them as static/shared libs that the
# inference/audio modules link against.

# ---- llama.cpp (LLM + VLM + embeddings) -----------------------------------
set(LLAMA_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER  OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL          OFF CACHE BOOL "" FORCE)
if(POLYMATH_USE_CUDA)
  set(GGML_CUDA ON CACHE BOOL "" FORCE)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp/CMakeLists.txt)
  add_subdirectory(llama.cpp)
endif()

# ---- whisper.cpp (ASR) -----------------------------------------------------
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/whisper.cpp/CMakeLists.txt)
  add_subdirectory(whisper.cpp)
endif()

# ---- Piper (TTS) -----------------------------------------------------------
# Piper's phonemization + voice synthesis. Linked by src/audio/tts_piper.
# (Built via its own CMake; some setups vendor only libpiper.)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/piper/CMakeLists.txt)
  add_subdirectory(piper EXCLUDE_FROM_ALL)
endif()

# ---- Header-only deps ------------------------------------------------------
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)

add_library(hnswlib INTERFACE)
target_include_directories(hnswlib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/hnswlib)
