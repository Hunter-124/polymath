# Locates a prebuilt ONNX Runtime (GPU) package and exposes target
# onnxruntime::onnxruntime.  Point POLYMATH_ONNXRUNTIME_ROOT (or env
# ONNXRUNTIME_ROOT) at the extracted onnxruntime-win-x64-gpu-*.zip.
#
#   onnxruntime::onnxruntime  -> include dir + onnxruntime.lib (+ DLLs to deploy)

set(_ort_root "${POLYMATH_ONNXRUNTIME_ROOT}")
if(NOT _ort_root)
  set(_ort_root "$ENV{ONNXRUNTIME_ROOT}")
endif()

find_path(ONNXRuntime_INCLUDE_DIR
  NAMES onnxruntime_cxx_api.h
  HINTS "${_ort_root}/include"
  PATH_SUFFIXES onnxruntime onnxruntime/core/session)

find_library(ONNXRuntime_LIBRARY
  NAMES onnxruntime
  HINTS "${_ort_root}/lib")

find_file(ONNXRuntime_DLL
  NAMES onnxruntime.dll
  HINTS "${_ort_root}/lib" "${_ort_root}/bin")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ONNXRuntime
  REQUIRED_VARS ONNXRuntime_LIBRARY ONNXRuntime_INCLUDE_DIR)

if(ONNXRuntime_FOUND AND NOT TARGET onnxruntime::onnxruntime)
  add_library(onnxruntime::onnxruntime UNKNOWN IMPORTED)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ONNXRuntime_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRuntime_INCLUDE_DIR}")
  # Expose the DLL so app packaging can copy it next to Polymath.exe.
  set(ONNXRuntime_RUNTIME_DLL "${ONNXRuntime_DLL}" CACHE FILEPATH "onnxruntime.dll to deploy")
endif()
