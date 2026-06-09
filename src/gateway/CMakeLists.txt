# gateway — HTTP + WebSocket bridge for the mobile/web client, plus the outbound
# relay tunnel for remote access and device-token auth.
#
# Depends ONLY on pm_core + Qt (NOT pm_app), so it can be linked into the app
# without creating a dependency cycle: the app talks to it through the abstract
# IAssistantBridge (bridge.h), which AppController implements during wiring.
#
# Qt 6.4+.  HttpServer + WebSockets are NOT requested by the root find_package
# (the rest of the app doesn't need them), so we pull them in here — additive and
# module-local, exactly like src/agent does for PrintSupport.
#
#   DEFERRED WIRING (see README.md): add the qthttpserver + qtwebsockets vcpkg
#   features and the Qt6 COMPONENTS to the ROOT build, then add_subdirectory()
#   this folder and link pm_gateway into the app target.

find_package(Qt6 REQUIRED COMPONENTS Core Network HttpServer WebSockets)

qt_add_library(pm_gateway STATIC
    gateway_service.cpp
    gateway_db.cpp
    auth.cpp
    json_map.cpp
    http_router.cpp
    http_server.cpp
    ws_hub.cpp
    relay_client.cpp
)

target_include_directories(pm_gateway PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# AUTOMOC for the QObject-derived classes (GatewayService, HttpServer, WsHub,
# RelayClient).  qt_add_library enables it, but set it explicitly for clarity.
set_target_properties(pm_gateway PROPERTIES AUTOMOC ON)

target_link_libraries(pm_gateway
    PUBLIC
        pm_core
        Qt6::Core
        Qt6::Network
        Qt6::HttpServer
        Qt6::WebSockets
        nlohmann_json::nlohmann_json
)

# Public headers (the surface AppController wires to): bridge.h gateway_service.h
