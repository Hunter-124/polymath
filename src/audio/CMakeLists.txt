# audio — capture/wakeword/vad/asr/tts pipeline + Wi-Fi satellite network input.
# Links whisper (third_party, ASR), onnxruntime (wakeword + Silero VAD),
# miniaudio (WASAPI capture + playback), and piper (TTS).
qt_add_library(pm_audio STATIC
    audio_service.cpp
    capture.cpp
    network_audio_source.cpp
    onnx_util.cpp
    wakeword.cpp
    vad.cpp
    asr_whisper.cpp
    tts_piper.cpp
)
target_include_directories(pm_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pm_audio PUBLIC pm_core
    PRIVATE miniaudio onnxruntime::onnxruntime)

# Opus decode for network satellites (optional, off by default).
# Enable with:  cmake -DPOLYMATH_USE_OPUS=ON ...
option(POLYMATH_USE_OPUS "Decode Opus datagrams from Wi-Fi satellites" OFF)
if(POLYMATH_USE_OPUS)
    find_package(Opus REQUIRED)
    target_link_libraries(pm_audio PRIVATE Opus::opus)
    target_compile_definitions(pm_audio PRIVATE POLYMATH_USE_OPUS=1)
    message(STATUS "pm_audio: Opus decode enabled")
endif()

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
