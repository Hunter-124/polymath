# app — AppController facade + the Polymath executable.
qt_add_library(pm_app STATIC
    app_controller.cpp
    app_bridge.cpp        # IAssistantBridge adapter wiring AppController -> pm_gateway
)
target_include_directories(pm_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_app PUBLIC
    pm_core pm_inference pm_scheduler pm_audio pm_vision
    pm_memory pm_agent pm_personality
    # pm_sessions: external agent CLI sessions (Claude Code / Codex / PTY).
    pm_sessions
    # pm_ui supplies the Wave-3 data models + camera image provider that
    # AppController owns and registers with the QML engine.
    pm_ui
    # pm_gateway: the mobile/web bridge. AppController owns a GatewayService and
    # implements its IAssistantBridge seam (via AppBridge).
    pm_gateway
    Qt6::Qml Qt6::Quick Qt6::Network
)

qt_add_executable(Polymath WIN32 main.cpp)
# WebEngine comes transitively via pm_ui (PUBLIC); re-find for main.cpp headers.
find_package(Qt6 REQUIRED COMPONENTS WebEngineQuick WebEngineCore)
target_link_libraries(Polymath PRIVATE
    pm_app pm_ui
    Qt6::Quick Qt6::QuickControls2
    Qt6::WebEngineQuick Qt6::WebEngineCore)

# Deploy runtime DLLs (Qt via windeployqt, ONNX Runtime, CUDA) — see docs/PACKAGING.md.
# Copy all onnxruntime*.dll (core + CUDA/TRT providers when GPU package is used).
if(WIN32 AND DEFINED ONNXRuntime_RUNTIME_DLL AND EXISTS "${ONNXRuntime_RUNTIME_DLL}")
    get_filename_component(_ort_dll_dir "${ONNXRuntime_RUNTIME_DLL}" DIRECTORY)
    file(GLOB _ort_dlls "${_ort_dll_dir}/onnxruntime*.dll")
    foreach(_dll IN LISTS _ort_dlls)
        add_custom_command(TARGET Polymath POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dll}" "$<TARGET_FILE_DIR:Polymath>")
    endforeach()
endif()
