cmake_minimum_required(VERSION 3.25)
project(Polymath VERSION 0.1.0 LANGUAGES C CXX)   # C: sqlite3 amalgamation + ggml

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(POLYMATH_USE_CUDA "Build llama.cpp / whisper.cpp with CUDA acceleration" ON)
option(POLYMATH_BUILD_TESTS "Build unit/integration tests" ON)

if(MSVC)
  # Gate every MSVC "/flag" with $<COMPILE_LANGUAGE> so it reaches C/C++ but NOT CUDA:
  # CMake forwards COMPILE_OPTIONS verbatim to nvcc (no -Xcompiler wrap), and a raw
  # "/flag" makes nvcc abort ("nvcc fatal: A single input file is required for a
  # non-link phase..." — it misparses the /flag as an input file).
  #
  # /Zc:char8_t- keeps u8"" literals as const char* for any C++20 translation unit. It
  # is added via add_compile_options (NOT CMAKE_CXX_FLAGS) on purpose: COMPILE_OPTIONS
  # are emitted *after* the /std:c++NN flag, and since /std:c++20 re-enables char8_t the
  # later flag must win. (The vendored engines dodge char8_t entirely by building at
  # C++17 — see the CMAKE_CXX_STANDARD scope around add_subdirectory(third_party) below.)
  add_compile_options(
    "$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>"
    "$<$<COMPILE_LANGUAGE:C,CXX>:/MP>"
    "$<$<COMPILE_LANGUAGE:C,CXX>:/EHsc>"
    "$<$<COMPILE_LANGUAGE:CXX>:/Zc:char8_t->")
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Concurrent Network)
qt_standard_project_setup(REQUIRES 6.5)

find_package(OpenCV CONFIG REQUIRED)
# sqlite3 is vendored (amalgamation, static) in third_party/ and exposed as the
# aliased target unofficial::sqlite3::sqlite3 — avoids the vcpkg sqlite3 DLL.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# ONNX Runtime (GPU) — prebuilt package located via POLYMATH_ONNXRUNTIME_ROOT.
# Wrapped in cmake/FindONNXRuntime.cmake (created in Wave 0) into target onnxruntime::onnxruntime.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
find_package(ONNXRuntime REQUIRED)

# Vendored native inference engines (built with CUDA when POLYMATH_USE_CUDA=ON).
# Build them at their native C++17. ggml already pins 17 ("don't bump"), but llama's
# src/ + common/ and whisper otherwise inherit our global C++20 — which enables char8_t
# and splits the llama tree irreconcilably: llama-chat.cpp passes u8"" literals where a
# const char* is wanted (needs char8_t OFF) while common/vendor/nlohmann/json.hpp uses
# std::u8string (needs char8_t ON). C++17 has no char8_t, so neither path triggers.
# Our own modules below stay at C++20 (restored after the subdirectory).
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(third_party)
set(CMAKE_CXX_STANDARD 20)

# ---------------------------------------------------------------------------
# Application modules.  core is the shared-contract layer everything depends on.
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/inference)
add_subdirectory(src/scheduler)
add_subdirectory(src/audio)
add_subdirectory(src/vision)
add_subdirectory(src/memory)
add_subdirectory(src/agent)
add_subdirectory(src/personality)
add_subdirectory(src/app)
add_subdirectory(src/ui)

if(POLYMATH_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
