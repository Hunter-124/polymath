# audio — capture/wakeword/vad/asr/tts. Wave-1 agent adds the pipeline sources
# and links whisper (third_party), onnxruntime (wakeword+vad), miniaudio, piper.
qt_add_library(pm_audio STATIC
    audio_service.cpp
)
target_include_directories(pm_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_audio PUBLIC pm_core
    PRIVATE miniaudio onnxruntime::onnxruntime)
if(TARGET whisper)
    target_link_libraries(pm_audio PRIVATE whisper)
    target_compile_definitions(pm_audio PRIVATE POLYMATH_HAVE_WHISPER=1)
endif()
