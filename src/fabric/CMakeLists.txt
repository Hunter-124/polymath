# fabric — the device fabric: bridges autonomous edge devices (cameras, voice
# satellites, lab instruments, panels) onto Hearth's EventBus + SQLite contracts.
#
# Depends only on pm_core + Qt::Core + nlohmann_json (no pm_app / pm_gateway), so
# it can be linked into the gateway (and later the app) without a cycle.
#
# The MQTT transport is OPTIONAL: enable -DPOLYMATH_USE_MQTT=ON to compile the
# Paho-based broker bridge (mqtt_paho.cpp). Off by default — the HTTP ingest plane
# (gateway routes) works without any broker, so the base build adds no new deps.

option(POLYMATH_USE_MQTT "Build the FabricService MQTT (Paho) transport" OFF)

find_package(Qt6 REQUIRED COMPONENTS Core)

set(PM_FABRIC_SOURCES
    device_registry.cpp
    fabric_service.cpp
)

if(POLYMATH_USE_MQTT)
    list(APPEND PM_FABRIC_SOURCES mqtt_paho.cpp)
endif()

qt_add_library(pm_fabric STATIC ${PM_FABRIC_SOURCES})

target_include_directories(pm_fabric PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(pm_fabric PROPERTIES AUTOMOC OFF)

target_link_libraries(pm_fabric
    PUBLIC
        pm_core
        Qt6::Core
        nlohmann_json::nlohmann_json
)

if(POLYMATH_USE_MQTT)
    # Eclipse Paho C++ (vcpkg: paho-mqttpp3). Linked only in the MQTT build.
    find_package(PahoMqttCpp CONFIG REQUIRED)
    target_link_libraries(pm_fabric PUBLIC PahoMqttCpp::paho-mqttpp3)
    target_compile_definitions(pm_fabric PUBLIC POLYMATH_USE_MQTT)
endif()
