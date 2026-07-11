# Third-party notices

Polymath links against and/or ships with the following open-source components.
Their licenses apply to those components; see each project for full text.

| Component | Role | Typical license |
|-----------|------|-----------------|
| Qt 6 | UI framework | LGPL / commercial (kit-dependent) |
| llama.cpp / ggml | Local LLM inference | MIT |
| whisper.cpp | Speech-to-text | MIT |
| ONNX Runtime | Perception + wake/VAD | MIT |
| OpenCV | Vision utilities | Apache-2.0 |
| Piper | TTS fallback | MIT |
| Kokoro (worker) | Neural TTS | Apache-2.0 (upstream model/terms) |
| SQLCipher / SQLite | Encrypted DB | BSD-style / public domain |
| OpenSSL | Crypto backend | Apache-2.0 |
| nlohmann/json | JSON | MIT |
| hnswlib | Vector index | Apache-2.0 |
| spdlog / fmt | Logging | MIT |
| Inter font | UI typeface | SIL Open Font License (`src/ui/fonts/LICENSE.txt`) |

Bundled scripts may download additional model weights (GGUF, ONNX, Kokoro);
those files remain under their respective model licenses and are not part of
this source tree.
