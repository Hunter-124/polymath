# vision — per-camera decode + motion + YOLO + face recognition + object finder.
qt_add_library(pm_vision STATIC
    vision_service.cpp
)
target_include_directories(pm_vision PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_vision PUBLIC pm_core pm_inference
    PRIVATE ${OpenCV_LIBS} onnxruntime::onnxruntime)
target_include_directories(pm_vision PRIVATE ${OpenCV_INCLUDE_DIRS})
