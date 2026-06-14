# Vendored native sub-engines.  Each is a git submodule (see ../.gitmodules).
# We forward the CUDA option down and build them as static/shared libs that the
# inference/audio modules link against.

# Single-binary mode (POLYMATH_BACKEND_DL): build ggml's backends as runtime-
# loadable libraries (ggml-cpu / ggml-cuda DLLs) selected at startup, so one
# Hearth.exe runs on GPU or CPU. This requires the engines to be SHARED so the
# backend modules register against a shared ggml-base. Scoped to this directory:
# `set(BUILD_SHARED_LIBS ...)` here does NOT leak to src/* (added by the parent
# after this subdirectory returns), so the pm_* libs stay static as declared.
if(POLYMATH_BACKEND_DL)
  set(BUILD_SHARED_LIBS ON)
  set(GGML_BACKEND_DL ON CACHE BOOL "" FORCE)
  set(GGML_CPU        ON CACHE BOOL "" FORCE)
  # GGML_BACKEND_DL forbids GGML_NATIVE (-march=native bakes in THIS machine's CPU
  # features, defeating a portable binary). GGML_CPU_ALL_VARIANTS instead builds a
  # ggml-cpu DLL per ISA tier (SSE4.2 / AVX2 / AVX512 / …) and loads the best one
  # the running CPU supports — so one binary is both portable AND fast.
  set(GGML_NATIVE           OFF CACHE BOOL "" FORCE)
  set(GGML_CPU_ALL_VARIANTS ON  CACHE BOOL "" FORCE)
  message(STATUS "polymath: GGML_BACKEND_DL ON — ggml backends build as runtime-loadable DLLs (portable CPU variants)")
endif()

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

# ---- SQLite / SQLCipher (vendored amalgamation, built static) --------------
# Compiled into the app (no sqlite3.dll) so there is exactly one sqlite3 with our
# CRT — the vcpkg sqlite3 DLL produced spurious SQLITE_NOMEM at runtime. Aliased
# to the vcpkg target name (unofficial::sqlite3::sqlite3) so the module
# CMakeLists link it unchanged.
#
# At-rest encryption: when the SQLCipher amalgamation is vendored under
# third_party/sqlcipher/ AND OpenSSL (libcrypto) is available, we compile THAT
# amalgamation instead — with SQLITE_HAS_CODEC + SQLITE_TEMP_STORE=2 + the
# OpenSSL crypto provider — so `PRAGMA key` really ciphers the file on disk and
# Database::encryptionActive() returns true. This is ONE static code path shared
# by both the VS (build/cpu) and Ninja/CUDA (build/cuda) builds. If SQLCipher or
# OpenSSL is absent we fall back to the plain amalgamation (key path is a no-op,
# reported honestly by Database::encryptionActive()).
set(_sqlcipher_amalg ${CMAKE_CURRENT_SOURCE_DIR}/sqlcipher/sqlite3.c)
if(EXISTS ${_sqlcipher_amalg})
  # libcrypto is the only OpenSSL piece SQLCipher needs (no TLS). Dynamic
  # x64-windows triplet (/MD) matches the app CRT; the one runtime DLL
  # (libcrypto-3-x64.dll) is deployed by the build scripts.
  find_package(OpenSSL QUIET COMPONENTS Crypto)
endif()

if(EXISTS ${_sqlcipher_amalg} AND OpenSSL_FOUND)
  message(STATUS "polymath: at-rest encryption ENABLED — vendoring SQLCipher amalgamation (OpenSSL backend)")
  add_library(sqlite3_local STATIC ${_sqlcipher_amalg})
  target_include_directories(sqlite3_local PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sqlcipher)
  target_compile_definitions(sqlite3_local PRIVATE
      SQLITE_ENABLE_FTS5
      SQLITE_THREADSAFE=1
      SQLITE_HAS_CODEC=1          # turn on the codec hooks (PRAGMA key, etc.)
      SQLITE_TEMP_STORE=2         # required by SQLCipher: temp data never on disk
      SQLCIPHER_CRYPTO_OPENSSL=1) # use OpenSSL libcrypto as the crypto provider
  target_link_libraries(sqlite3_local PRIVATE OpenSSL::Crypto)
  # Mark the build so downstream code can rely on the codec at runtime.
  target_compile_definitions(sqlite3_local PUBLIC POLYMATH_SQLCIPHER=1)
else()
  if(EXISTS ${_sqlcipher_amalg})
    message(WARNING "polymath: SQLCipher amalgamation present but OpenSSL not found — "
                    "falling back to PLAIN sqlite3 (at-rest encryption will be INACTIVE). "
                    "Install OpenSSL (vcpkg install openssl) to enable the codec.")
  endif()
  add_library(sqlite3_local STATIC ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3/sqlite3.c)
  target_include_directories(sqlite3_local PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3)
  target_compile_definitions(sqlite3_local PRIVATE SQLITE_ENABLE_FTS5 SQLITE_THREADSAFE=1)
endif()
add_library(unofficial::sqlite3::sqlite3 ALIAS sqlite3_local)
