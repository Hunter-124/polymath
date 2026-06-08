# Vendored native sub-engines.  Each is a git submodule (see ../.gitmodules).
# We forward the CUDA option down and build them as static/shared libs that the
# inference/audio modules link against.

# ---- llama.cpp (LLM + VLM + embeddings) -----------------------------------
set(LLAMA_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER  OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL          OFF CACHE BOOL "" FORCE)
# Tools include `mtmd` (multimodal/VLM) — needed for describeImage()/"find my
# keys". ON now that the mtmd API is reconciled. Targeted builds (pm_inference)
# only compile the `mtmd` library, not the standalone tool executables.
set(LLAMA_BUILD_TOOLS   ON CACHE BOOL "" FORCE)
# mtmd lives under tools/, which is only added when COMMON is also built (it
# defaults OFF for a subdirectory build). Targeting pm_inference still compiles
# only the mtmd + common libs, not the standalone tool executables.
set(LLAMA_BUILD_COMMON  ON CACHE BOOL "" FORCE)
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

# ---- SQLite (vendored amalgamation, built static) --------------------------
# Compiled into the app (no sqlite3.dll) so there is exactly one sqlite3 with our
# CRT — the vcpkg sqlite3 DLL produced spurious SQLITE_NOMEM at runtime. Aliased
# to the vcpkg target name so the module CMakeLists link it unchanged.
add_library(sqlite3_local STATIC ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3/sqlite3.c)
target_include_directories(sqlite3_local PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3)
target_compile_definitions(sqlite3_local PRIVATE SQLITE_ENABLE_FTS5 SQLITE_THREADSAFE=1)
add_library(unofficial::sqlite3::sqlite3 ALIAS sqlite3_local)
