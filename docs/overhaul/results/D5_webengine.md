# D5 results (2026-07-10)

## QtWebEngine install
```
aqt install-qt windows desktop 6.6.3 win64_msvc2019_64 -m qtwebengine qtwebchannel qtpositioning --outputdir C:\Qt
```
Modules present: Qt6WebEngineCore, Qt6WebEngineQuick, QtWebEngineProcess.

## Code
- `web_adblock_interceptor.*` — blocks common ad/tracker hosts + YT ad paths
- `WebSurface.qml` — WebEngineView + YouTube clean-mode JS (hide ad chrome, auto-skip, mute mid-rolls)
- `main.cpp` — QtWebEngineQuick::initialize(), AA_ShareOpenGLContexts, default-profile interceptor
- CMake: link Qt6::WebEngineQuick/Core on pm_ui + Polymath
- SurfaceHost: `video` type → WebSurface (slop_mode YouTube)
- build-cpu.ps1: deploy WebEngine resources/locales/process

## Piper TTS
- `data/models/piper-engine/piper.exe` + espeak-ng-data from rhasspy 2023.11.14-2 release
- `fetch-models.ps1` now fetches piper engine automatically
- test_audio_e2e: TTS=true, engine path resolved

## Verify
- Polymath.exe Release build: OK
- capture_views 13/13: OK
- test_audio_e2e: OK (wake/vad/tts ready)
- windeployqt: QtWebEngineProcess + resources present

## GPU
- build/deps/cuda/toolkit not present — CUDA tree not built this session
- build-gpu.ps1 updated: arch default 75, VS 18 vcvars path
