# D4 — TTS v2: verification notes / deviations

## Default voice

Chosen default: **`af_heart`**.

Verified present in the installed `data/models/kokoro-engine/voices-v1.0.bin`
by loading it directly with the venv's numpy (no ONNX model load needed):

```
data/models/kokoro-engine/venv/Scripts/python.exe -c "
import numpy as np
v = np.load('data/models/kokoro-engine/voices-v1.0.bin')
print(sorted(v.keys()))
"
```

Full dump (54 voices total; the ones D4 targets — English af_*/am_*/bf_*/bm_*
— are all present):

```
af_alloy, af_aoede, af_bella, af_heart, af_jessica, af_kore, af_nicole,
af_nova, af_river, af_sarah, af_sky, am_adam, am_echo, am_eric, am_fenrir,
am_liam, am_michael, am_onyx, am_puck, am_santa, bf_alice, bf_emma,
bf_isabella, bf_lily, bm_daniel, bm_fable, bm_george, bm_lewis,
(+ 26 other-locale voices: ef_/em_/ff_/hf_/hm_/if_/im_/jf_/jm_/pf_/pm_/zf_/zm_)
```

`af_heart` exists — no substitution needed. `tts.voice` defaults to it in
`config.cpp::seedDefaults()`, and `TtsPiper`'s internal fallback (when a
requested voice is empty/unrecognised) also uses `af_heart` instead of the
old flatter `af_sky`.

`tools/kokoro_worker/kokoro_worker.py --list-voices` (new mode, see below) was
verified against the real file and produces the same list, confirming the
worker-side enumeration path also works end to end if ever wired to a live
subprocess call instead of the static Settings list.

## Config keys added (`src/core/config.h` / `.cpp`)

| Key | Default | Range |
|---|---|---|
| `tts.engine` | `auto` | `auto` \| `kokoro` \| `piper` |
| `tts.voice` | `af_heart` | any shipped Kokoro id or legacy Piper id |
| `tts.speed` | `1.0` | UI clamps 0.8–1.3 |
| `tts.volume` | `1.0` | UI clamps 0.0–1.5 |

## How engine/voice/speed/volume flow now

1. **Startup** (`AudioService::Impl::loadModels()`, `src/audio/audio_service.cpp`):
   reads all four keys from `Config`, calls `tts.setEnginePreference(engine)`
   then `tts.init(models/"piper", voice, output_device)`, then
   `tts.setSpeed()`/`tts.setVolume()`.
2. **`TtsPiper::init()`** (`src/audio/tts_piper.cpp`) now branches on the
   engine preference instead of always autodetecting: `piper` always skips
   Kokoro even if installed; `kokoro` forces it and warns+falls back to Piper
   only if the worker/model files are actually missing; `auto` is the
   original file-presence behaviour, unchanged.
3. **Live updates** (no restart, no new EventBus type): `TtsWorker::process()`
   / `TtsWorker::warmUp()` (both in `audio_service.cpp`, internal to the TTS
   QThread) call a new private `applyLiveTtsSettings()` that re-reads
   `tts.speed` / `tts.volume` / `tts.voice` from `Config` (thread-safe,
   WAL+mutex) before every utterance and pushes them into `TtsPiper` via
   `setSpeed()` / `setVolume()` / `setDefaultVoice()`. This means any Settings
   change — including the Preview button — is reflected on the *next*
   spoken line with no plumbing beyond a Database re-read (cheap next to
   synthesis cost).
   - `tts.engine` is deliberately **not** live-applied this way: switching
     Kokoro↔Piper mid-session means tearing down/rebuilding the persistent
     subprocess and its playback pipeline. Given Kokoro is already the
     active/decided engine (00_MASTER_PLAN.md §2/§3.5) and the DAG's own
     accept criterion only requires voice/speed to be live, engine changes
     take effect on the next app start. The Settings UI says so under the
     combo.
4. **Per-utterance voice**: `TtsPiper::speak(text, voice, append)` (voice ==
   persona voice from `AgentLoop`, or empty for the global default) →
   `Impl::mapVoice()` → resolves to a `kokoro-<id>` string, with the global
   `tts.voice` default only used when the caller passes an empty voice —
   i.e. **persona voice overrides the global default**, matching the DAG
   requirement. No `personality_manager.cpp` change was needed: it already
   parses `persona.json`'s `voice` field as a raw pass-through string
   (`p.voice = j.value("voice", "")`, `src/personality/personality_manager.cpp:114`)
   with no mapping logic of its own — all mapping happens in
   `TtsPiper::Impl::mapVoice()`, which is the file this node owns. Per the
   task's own phrasing ("Edit ONLY the mapVoice/voice-selection logic in
   personality_manager if needed"), it wasn't needed, so
   `personality_manager.cpp` is untouched.
5. **Speed** reaches the worker two ways: (a) `--speed` CLI arg +
   `KOKORO_SPEED` env var when a fresh Kokoro process is spawned
   (`ensureProc()`), and (b) an inline `!speed=<f>` control line written to
   an already-running process (`TtsPiper::setSpeed()`), which the worker
   already supported.
6. **Volume** is applied as an int16 gain multiply (clamped, no wraparound)
   right before PCM is pushed onto the playback queue
   (`Impl::enqueue()`) — after synthesis, so `synthesize()`/
   `synthesizeSentences()` (test/fixture paths) still return raw,
   unscaled PCM.

## mapVoice() coverage

`TtsPiper::Impl::mapVoice()` now validates against a
`shippedKokoroVoices()` set (all 28 English af_*/am_*/bf_*/bm_* ids
confirmed present above). Behaviour:
- Known id (bare or `kokoro-`-prefixed) → used directly.
- af_*/am_*/bf_*/bm_*-shaped but not in the known set → tried anyway with a
  `PM_WARN` log line (forward-compatible with a future voices.bin drop).
- Legacy Piper name heuristics (alan/ryan/joe/amy/lessac/jenny/kathleen/...)
  → mapped to a same-gender-ish Kokoro voice, logged at INFO.
- Anything else → falls back to the configured default voice with a
  `PM_WARN` explaining why (so a persona typo doesn't silently go quiet).

## Chunking quality

- `splitSentences()` gained an abbreviation/decimal guard
  (`isAbbreviationPeriod()`): decimal numbers (`3.14`), single-letter
  tokens (covers initials and acronyms like `U.S.`, `e.g.`, `i.e.` since
  each letter between the dots is length 1), and a curated word list
  (`Dr.`, `Mr.`, `Mrs.`, `Ms.`, `Prof.`, `St.`, `vs.`, `etc.`, month
  abbreviations, day abbreviations, `Inc.`/`Ltd.`/`Co.`, etc.) no longer
  split a sentence. `splitSentences()`'s existing unit test
  (`tests/test_audio_e2e.cpp::checkTtsChunkOrdering`, "Hello there. How are
  you? I am fine!" → 3 chunks) is unaffected since none of those clauses
  match the abbreviation guard.
- Fragment merging (`mergeShortFragments()`, new, internal) folds
  consecutive sentences under 40 chars forward into the next one so short
  clauses aren't synthesized (and separately queued) in isolation — this is
  applied downstream of `splitSentences()` inside `speak()` and
  `synthesizeSentences()` only, **not** inside the public `splitSentences()`
  itself, specifically so the existing unit test (which asserts exact
  3-way splitting of three short sentences) keeps passing while the actual
  playback pipeline still gets the smoother, merged chunking.
- 280 ms idle-gap protocol (`kUtteranceIdleMs`) untouched.

## kokoro_worker.py

- Added `--list-voices` (numpy read of `--voices` only, no ONNX/model load,
  no `--model` required in that mode) — verified against the real
  installed `voices-v1.0.bin`, prints the same 28 English ids listed above
  plus the other-locale ones, one per line, exit 0.
- Added `!flush` inline no-op keepalive (acknowledges on stderr, emits no
  PCM) per the DAG's "if trivial" ask.
- Default voice arg/env fallback changed from `af_sky` to `af_heart` to
  match the new product default.
- Did not add espeak-ng phonemizer quality flags: `kokoro-onnx`'s `Kokoro`
  class doesn't expose a phonemizer-tuning parameter in this installed
  version (0.5.0) — it's an internal implementation detail, not a CLI/API
  knob — so there was nothing trivial to wire up. Noted here per the DAG's
  own "else skip" fallback.

## Not done / explicitly out of scope for this node

- **No rename to `tts_engine.*`.** The DAG node text allows it ("rename
  allowed") but not touching `CMakeLists.txt` / `capture.cpp` /
  `tests/test_audio_e2e.cpp` (none owned by this node) was safer than a
  rename that ripples into files this node doesn't own. Kept
  `tts_piper.cpp/.h`.
- **Engine hot-swap.** `tts.engine` changes apply on next app start (see
  "Live updates" above) — full engine hot-swap (tear down Kokoro process,
  rebuild Piper process/playback state, or vice versa, safely under
  concurrent synth/playback) was judged out of proportion for a config
  most users set once, and isn't required by the DAG's accept criterion
  (only voice/speed are required live).
- **Owner subjective sign-off** ("robotic check" listening test) — requires
  a live GPU/audio build and a human listener; not something this pass can
  execute. Left for the F2 live e2e acceptance run.
