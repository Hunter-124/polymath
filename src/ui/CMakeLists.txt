# ui — the Qt Quick (QML) module. URI "Polymath", loaded by main.cpp via
# engine.loadFromModule("Polymath", "Main"). Wave-3 UI agent expands the views
# and adds C++ QAbstractListModels (chat/shopping/cameras/tasks/timeline) plus
# the live-frame QQuickImageProvider, compiled into this same target so the QML
# and its backing C++ ship together.
# Style.qml is a QML singleton (pragma Singleton) — mark it so the module's
# generated qmldir registers it and `import Polymath` exposes `Style`.
set_source_files_properties(qml/Style.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# Nav.qml is the QML-layer message bus (navigation/notify signals) — also a
# singleton so isolated views and the Main shell share one instance.
set_source_files_properties(qml/Nav.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

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
        qml/PanelMode.qml
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
        qml/SettingsView.qml
        qml/CommandPalette.qml
        qml/Nav.qml
        # Self-contained QR encoder (MIT) for the Mobile Access pairing QR; pure
        # ECMAScript usable from the Canvas, no extra Qt module / C++ dependency.
        qml/qrcode.js
        # Shared style singleton + themed controls (cohesive dark theme over the
        # otherwise-unstyled QtQuick.Controls.Basic).
        qml/Style.qml
        qml/controls/PmButton.qml
        qml/controls/PmTextField.qml
        qml/controls/PmComboBox.qml
        qml/controls/PmSwitch.qml
        qml/controls/PmCheckBox.qml
        qml/controls/PmItemDelegate.qml
        qml/controls/PmToolButton.qml
        qml/controls/PmIcon.qml
        qml/controls/PmIconButton.qml
        qml/controls/EmptyState.qml
    # Bundle the Inter (SIL OFL) UI font so QFontDatabase always finds a family
    # and the app renders identically headless and on any machine.
    RESOURCES
        fonts/Inter.ttf
    SOURCES
        models/chat_model.h        models/chat_model.cpp
        models/shopping_model.h    models/shopping_model.cpp
        models/camera_model.h      models/camera_model.cpp
        models/task_model.h        models/task_model.cpp
        models/timeline_model.h    models/timeline_model.cpp
        camera_image_provider.h    camera_image_provider.cpp
)

# Headers are consumed by pm_app (AppController) and by the QML engine wiring,
# so expose both this dir and the models/ subdir to dependents.
target_include_directories(pm_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/models
)

# pm_core gives the models the Database wrapper, EventBus payload types, types.h
# and the spdlog logging macros; Qt Gui supplies QImage for the image provider.
target_link_libraries(pm_ui PUBLIC pm_core Qt6::Quick Qt6::QuickControls2 Qt6::Gui)

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
