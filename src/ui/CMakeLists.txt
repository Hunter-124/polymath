# ui — the Qt Quick (QML) module. URI "Polymath", loaded by main.cpp via
# engine.loadFromModule("Polymath", "Main"). Wave-3 UI agent expands the views
# and adds C++ QAbstractListModels (chat/shopping/cameras/tasks/timeline) plus
# the live-frame QQuickImageProvider, compiled into this same target so the QML
# and its backing C++ ship together.
qt_add_qml_module(pm_ui
    STATIC          # static so the model/provider symbols link into pm_app and
                    # the whole app stays a single binary (no pm_ui.dll)
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
