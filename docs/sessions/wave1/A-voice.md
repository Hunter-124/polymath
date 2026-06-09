# Wave 1 · Card A — Voice loop, end-to-end

**Read [`../SHARED.md`](../SHARED.md) first.**  Owns: **`src/audio/` only.**

## Goal
Prove the audio pipeline works end-to-end and fix what doesn't:
`capture → wake word → VAD → ASR (whisper) → [emit Utterance] → … → [SpeakRequest] → TTS (Piper)`.
Treat AgentRuntime as a black box — you only confirm an `Utterance` goes OUT on the EventBus and
a `SpeakRequest` comes back IN and is spoken.

## Verify (recorded WAV — never the live mic)
1. **ASR** — feed a known 16 kHz mono WAV through the whisper path → transcript matches expected
   text (allow minor variance).
2. **Wake word + VAD** — clip with "Hey Jarvis" + speech → assert `WakeWordDetected` then an
   `Utterance{text}` is published; a silence/noise clip → no `Utterance` (VAD gates it).
3. **TTS** — publish `SpeakRequest{text, voice}` → Piper (prebuilt `piper.exe` via QProcess)
   produces audio; capture its output to a WAV and assert non-empty / plausible duration.
4. **Privacy gate** — with the mic / ambient-transcription toggle OFF, capture and ambient ASR
   must not run.

## How
- Read `src/audio/*` (capture, wakeword, vad, asr_whisper, tts_piper) and how `AudioService`
  publishes on the EventBus.
- `tests/test_audio_e2e.cpp` constructs the pieces (or `AudioService`) and pumps a fixture WAV
  through them. Fixtures in `tests/fixtures/audio/` — you can synthesize a spoken WAV with Piper.

## Done when
`ctest -R audio` passes: WAV→expected transcript; wake+VAD→Utterance; SpeakRequest→non-empty TTS
WAV; privacy-off→silence. Report at `docs/sessions/reports/A-voice.md`.
