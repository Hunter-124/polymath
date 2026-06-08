# ui — the Qt Quick (QML) module. URI "Polymath", loaded by main.cpp via
# engine.loadFromModule("Polymath", "Main"). Wave-3 UI agent expands the views
# and adds C++ QAbstractListModels (chat/shopping/cameras/tasks/timeline).
qt_add_qml_module(pm_ui
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
)
target_link_libraries(pm_ui PUBLIC Qt6::Quick Qt6::QuickControls2)
