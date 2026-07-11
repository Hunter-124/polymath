# ui — the Qt Quick (QML) module. URI "Polymath", loaded by main.cpp via
# engine.loadFromModule("Polymath", "Main"). Wave-3 UI agent expands the views
# and adds C++ QAbstractListModels (chat/shopping/cameras/tasks/timeline) plus
# the live-frame QQuickImageProvider, compiled into this same target so the QML
# and its backing C++ ship together.
# Style.qml / Icons.qml are QML singletons (pragma Singleton) — mark them so the
# module's generated qmldir registers them and `import Polymath` exposes them.
set_source_files_properties(qml/Style.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/theme/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(pm_ui
    STATIC          # static so the model/provider symbols link into pm_app and
                    # the whole app stays a single binary (no pm_ui.dll)
    NO_PLUGIN       # This module registers ZERO C++ QML types (the data models
                    # are context properties, the camera feed is an image
                    # provider) — every QML type here is a .qml file.  Without a
                    # plugin the generated qmldir drops its `plugin` line, so the
                    # engine resolves `import Polymath` (Style singleton + themed
                    # controls) straight from the embedded resources.  This is
                    # what lets src/app/main.cpp keep loading Main.qml by URL
                    # without importing a static QML plugin (which it cannot, and
                    # which Card F may not edit).
    URI Polymath
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Dashboard.qml
        qml/ChatView.qml
        qml/CamerasView.qml
        qml/TaskQueueView.qml
        qml/TimelineView.qml
        qml/ShoppingView.qml
        qml/PersonalitiesView.qml
        qml/ModelManagerView.qml
        qml/PrivacyView.qml
        qml/MobileAccessView.qml
        # Overhaul A2 placeholders (full UI in wave B/C).
        qml/SettingsView.qml
        qml/SettingsSection.qml
        qml/ToastStack.qml
        qml/NotificationBell.qml
        qml/NotificationCenter.qml
        qml/CommandPalette.qml
        qml/SurfaceHost.qml
        qml/AgentSessionsView.qml
        qml/surfaces/PlaceholderSurface.qml
        qml/surfaces/ImageSurface.qml
        qml/surfaces/WebSurface.qml
        # B2: results-picker grid for youtube_search results (type:"video_picker").
        qml/surfaces/VideoPickerSurface.qml
        # Self-contained QR encoder (MIT) for the Mobile Access pairing QR; pure
        # ECMAScript usable from the Canvas, no extra Qt module / C++ dependency.
        qml/qrcode.js
        # Holographic aurora design system (docs/overhaul/01_GUI_DESIGN_SYSTEM.md).
        qml/Style.qml
        qml/theme/Icons.qml
        qml/effects/AuroraBackground.qml
        qml/controls/GlassPanel.qml
        qml/controls/GlassCard.qml
        qml/controls/GlassSurface.qml
        qml/controls/PmButton.qml
        qml/controls/PmTextField.qml
        qml/controls/PmComboBox.qml
        qml/controls/PmSwitch.qml
        qml/controls/PmCheckBox.qml
        qml/controls/PmItemDelegate.qml
        qml/controls/PmToolButton.qml
        qml/controls/PmIcon.qml
        qml/controls/EmptyState.qml
        qml/controls/PmTooltip.qml
        qml/controls/PmBadge.qml
        qml/controls/PmPill.qml
        qml/controls/PmStatusDot.qml
        qml/controls/PmSectionHeader.qml
        qml/controls/PmScrollBar.qml
        qml/controls/PmDialog.qml
        qml/controls/PmSlider.qml
    # Bundle the Inter (SIL OFL) UI font so QFontDatabase always finds a family
    # and the app renders identically headless and on any machine. Tray icon is
    # used by Main.qml's SystemTrayIcon so close/minimize is recoverable.
    RESOURCES
        fonts/Inter.ttf
        icons/tray.png
        # B3: standalone YouTube clean-mode script (page-context JS, injected
        # into WebSurface's WebEngineView by B2). Listed under RESOURCES (not
        # QML_FILES) since it is raw text read at runtime and runJavaScript()'d
        # into the web page — it is never `import`ed as a QML JS module, so it
        # must not go through qmlcachegen/qmllint's QML-JS-module processing.
        # Resolves at qrc:/qt/qml/Polymath/qml/surfaces/YtClean.js — see
        # docs/overhaul2/results/B3_contract.md for the injection contract.
        qml/surfaces/YtClean.js
    SOURCES
        models/chat_model.h           models/chat_model.cpp
        models/shopping_model.h       models/shopping_model.cpp
        models/camera_model.h         models/camera_model.cpp
        models/task_model.h           models/task_model.cpp
        models/timeline_model.h       models/timeline_model.cpp
        models/notifications_model.h  models/notifications_model.cpp
        models/sessions_model.h       models/sessions_model.cpp
        settings_controller.h         settings_controller.cpp
        camera_image_provider.h       camera_image_provider.cpp
        # D5: adblock interceptor for QtWebEngine surfaces
        web_adblock_interceptor.h     web_adblock_interceptor.cpp
)

# Headers are consumed by pm_app (AppController) and by the QML engine wiring,
# so expose both this dir and the models/ subdir to dependents.
target_include_directories(pm_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/models
)

# pm_core: Database, EventBus, Config. Qt Multimedia: QMediaDevices for settings
# audio pickers. QuickEffects optional for GlassSurface blur (not required A1/A2).
# D5: WebEngineQuick required for real WebSurface / YouTube.
find_package(Qt6 QUIET COMPONENTS QuickEffects)
find_package(Qt6 REQUIRED COMPONENTS WebEngineQuick WebEngineCore)
set(_pm_ui_qt Qt6::Quick Qt6::QuickControls2 Qt6::Gui Qt6::Multimedia
              Qt6::WebEngineQuick Qt6::WebEngineCore)
if(TARGET Qt6::QuickEffects)
    list(APPEND _pm_ui_qt Qt6::QuickEffects)
endif()
# pm_sessions: SessionsModel talks to AgentSessionService (spawn/send/stop).
target_link_libraries(pm_ui PUBLIC pm_core pm_sessions ${_pm_ui_qt})

# ---------------------------------------------------------------------------
# capture_views — offscreen screenshot harness (Card F).  Renders every view to
# PNG with a stub `app` + stub list models, so the dark theme / fonts / bindings
# can be verified headless with no backend, models, or GPU.  Linking pm_ui pulls
# in the embedded `Polymath` QML resources; with NO_PLUGIN the views' `import
# Polymath` (Style singleton + themed controls) resolves from those resources,
# exactly as it does in the real app.
# ---------------------------------------------------------------------------
qt_add_executable(capture_views tools/capture_views.cpp)
set_target_properties(capture_views PROPERTIES AUTOMOC ON)
target_link_libraries(capture_views PRIVATE
    pm_ui Qt6::Quick Qt6::QuickControls2 Qt6::Gui)

# ---------------------------------------------------------------------------
# Deploy Inter into the directory Qt scans for fonts at startup
# (<exedir>/lib/fonts).  Qt 6 no longer ships fonts, so on a windeployqt tree
# this directory is missing and QGuiApplication logs, before any QML runs:
#     QFontDatabase: Cannot find font directory <exedir>/lib/fonts
# Populating it with our bundled Inter both silences that warning and gives the
# platform a real default family.  We hang this off capture_views (a target we
# own); it shares the bin/Release output dir with Polymath.exe, so the font
# lands beside the app — and the default ("all") build always builds it.
# add_custom_command(TARGET ...) must reference a target created in THIS dir,
# which Polymath (src/app) is not — hence capture_views, not Polymath.
# ---------------------------------------------------------------------------
add_custom_command(TARGET capture_views POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:capture_views>/lib/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter.ttf"
            "$<TARGET_FILE_DIR:capture_views>/lib/fonts/Inter.ttf"
    COMMENT "Deploying Inter into lib/fonts (fixes QFontDatabase font-dir warning)")
