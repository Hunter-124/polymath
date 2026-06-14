cmake_minimum_required(VERSION 3.16)
project(hearth-cam-pi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(hearth-cam-pi
  src/main.cpp
  src/config.cpp
  src/auth.cpp
  src/mqtt.cpp
  src/httpd.cpp
  src/detector.cpp
)
target_include_directories(hearth-cam-pi PRIVATE include)
target_link_libraries(hearth-cam-pi PRIVATE pthread)

# --- OpenSSL (required: HMAC + RNG for auth) --------------------------------
find_package(OpenSSL REQUIRED)
target_link_libraries(hearth-cam-pi PRIVATE OpenSSL::Crypto)

# --- libmosquitto (optional: real MQTT; else HTTP-push-only) ----------------
find_library(MOSQUITTO_LIB mosquitto)
find_path(MOSQUITTO_INC mosquitto.h)
if(MOSQUITTO_LIB AND MOSQUITTO_INC)
  message(STATUS "hearth-cam-pi: libmosquitto found -> MQTT enabled")
  target_compile_definitions(hearth-cam-pi PRIVATE HEARTH_HAVE_MOSQUITTO)
  target_include_directories(hearth-cam-pi PRIVATE ${MOSQUITTO_INC})
  target_link_libraries(hearth-cam-pi PRIVATE ${MOSQUITTO_LIB})
else()
  message(WARNING "hearth-cam-pi: libmosquitto NOT found -> MQTT stubbed (HTTP push only). "
                  "Install libmosquitto-dev to enable.")
endif()

# --- ONNX Runtime (optional: real YOLO; else motion stub) -------------------
# Define HEARTH_HAVE_ONNX + link onnxruntime to enable the model (see detector.cpp).
find_library(ONNXRT_LIB onnxruntime)
if(ONNXRT_LIB AND DEFINED HEARTH_USE_ONNX)
  message(STATUS "hearth-cam-pi: onnxruntime enabled")
  target_compile_definitions(hearth-cam-pi PRIVATE HEARTH_HAVE_ONNX)
  target_link_libraries(hearth-cam-pi PRIVATE ${ONNXRT_LIB})
endif()

install(TARGETS hearth-cam-pi RUNTIME DESTINATION bin)
install(FILES systemd/hearth-cam-pi.service DESTINATION /etc/systemd/system)
