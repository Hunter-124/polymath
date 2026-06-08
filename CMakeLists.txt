cmake_minimum_required(VERSION 3.25)
project(Polymath VERSION 0.1.0 LANGUAGES CXX)

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
  add_compile_options(/utf-8 /MP /EHsc)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Concurrent Network)
qt_standard_project_setup(REQUIRES 6.5)

find_package(OpenCV CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# ONNX Runtime (GPU) — prebuilt package located via POLYMATH_ONNXRUNTIME_ROOT.
# Wrapped in cmake/FindONNXRuntime.cmake (created in Wave 0) into target onnxruntime::onnxruntime.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
find_package(ONNXRuntime REQUIRED)

# Vendored native inference engines (built with CUDA when POLYMATH_USE_CUDA=ON).
add_subdirectory(third_party)

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
