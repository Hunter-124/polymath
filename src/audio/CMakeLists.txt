# audio — capture/wakeword/vad/asr/tts pipeline.
# Links whisper (third_party, ASR), onnxruntime (wakeword + Silero VAD),
# miniaudio (WASAPI capture + playback), and piper (TTS).
qt_add_library(pm_audio STATIC
    audio_service.cpp
    capture.cpp
    onnx_util.cpp
    wakeword.cpp
    vad.cpp
    asr_whisper.cpp
    tts_piper.cpp
)
target_include_directories(pm_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_audio PUBLIC pm_core
    PRIVATE miniaudio onnxruntime::onnxruntime)

# whisper.cpp (ASR) — present only when the submodule is checked out.
if(TARGET whisper)
    target_link_libraries(pm_audio PRIVATE whisper)
    target_compile_definitions(pm_audio PRIVATE POLYMATH_HAVE_WHISPER=1)
endif()

# Piper (TTS) — vendored as `piper` (or `libpiper`) under third_party.
if(TARGET piper)
    target_link_libraries(pm_audio PRIVATE piper)
    target_compile_definitions(pm_audio PRIVATE POLYMATH_HAVE_PIPER=1)
elseif(TARGET libpiper)
    target_link_libraries(pm_audio PRIVATE libpiper)
    target_compile_definitions(pm_audio PRIVATE POLYMATH_HAVE_PIPER=1)
endif()
