# desktop — "computer use": screen capture (Win32 BitBlt) + input injection
# (SendInput) + UI Automation element targeting + a local-VLM coordinate fallback,
# orchestrated by DesktopController so the agent can drive the mouse/keyboard.
# Windows-only at the OS layer (degrades to no-ops on other platforms); links the
# inference module for the Vision model's describeImage().
qt_add_library(pm_desktop STATIC
    screen_capture.cpp
    input_injector.cpp
    ui_automation.cpp
    desktop_controller.cpp
    global_hotkey.cpp
)
# No Qt Designer .ui files here — turn off AUTOUIC so it doesn't misread the
# `ui_automation.h` include as a generated `ui_*.h` and hunt for `automation.ui`.
set_target_properties(pm_desktop PROPERTIES AUTOUIC OFF)
target_include_directories(pm_desktop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# pm_core: EventBus/types/logging (+ nlohmann via its PUBLIC deps); pm_inference:
# InferenceManager::describeImage; Qt6::Gui: QImage/JPEG encode for the screenshot.
target_link_libraries(pm_desktop PUBLIC pm_core pm_inference Qt6::Gui)
if(WIN32)
    # user32/gdi32: BitBlt + SendInput; ole32/oleaut32: COM for UI Automation.
    target_link_libraries(pm_desktop PRIVATE user32 gdi32 ole32 oleaut32)
endif()
